/*
 * src/build_pkg.c
 *
 * Copyright (c) 2024-2025 Omar Berrow
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <threads.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#include "package.h"
#include "path.h"
#include "lock.h"

#if HAS_LIBCURL

#include <curl/curl.h>
#include <curl/easy.h>

typedef CURL *curl_handle;

#define cleanup_curl curl_easy_cleanup
thread_local char* curl_err = NULL;

curl_handle init_curl()
{
    curl_handle hnd = curl_easy_init();
    if (!hnd)
        return hnd;
    curl_err = malloc(CURL_ERROR_SIZE*4);
    curl_easy_setopt(hnd, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(hnd, CURLOPT_ERRORBUFFER, curl_err);
    return hnd;
}

static char* download_archive(curl_handle hnd, const char* url)
{
    char *template = malloc(18);
    memcpy(template, "obos-strap-XXXXXX", 18);
    int fd = mkstemp(template);
    if (fd == -1)
    {
        perror("mkstemp");
        return NULL;
    }
    FILE* f = fdopen(fd, "w");
    if (!f)
    {
        perror("fopen");
        return NULL;
    }
    curl_easy_setopt(hnd, CURLOPT_URL, url);
    curl_easy_setopt(hnd, CURLOPT_WRITEDATA, f);
    CURLcode res = curl_easy_perform(hnd);
    if (res != CURLE_OK)
    {
        printf("Error while downloading %s:\n%s\n", url, curl_err);
        fclose(f);
        return NULL;
    }
    curl_easy_setopt(hnd, CURLOPT_WRITEDATA, NULL);
    fclose(f);
    return template;
}

static bool extract_archive(const char* url, const char* name, char* archive_path)
{
    // TODO: Use a library?
    string_array argv = {};
    string_array_append(&argv, "tar");
    string_array_append(&argv, "-xf");
    string_array_append(&argv, archive_path);
    int ret = run_command("tar", argv);
    if (ret != EXIT_SUCCESS)
        printf("Could not run program 'tar'. Exit status: %d\n", ret);
    return ret == EXIT_SUCCESS;
}

#else
#define init_curl() (-1)
#define cleanup_curl(c) (void)(c)
typedef int curl_handle;
static char* download_archive(curl_handle hnd, const char* url)
{
    (void)(hnd);
    printf("Could not download archive %s. You must build obos-strap with libcurl installed.\n", url);
    return NULL;
}
static bool extract_archive(const char* url, const char* name, char* archive_path)
{
    (void)(url);
    (void)(name);
    (void)(archive_path);
    return false;
}
#endif

// Applies a patch 'patch_path' to the file 'modifies_path'
// This is run in the initial CWD of the process.
static bool apply_patch(const char* patch_path, const char* modifies_path)
{
    char* patch = realpath(patch_path, NULL);
    // printf("Entering directory %s\n", repo_directory);
    if (chdir(repo_directory) == -1)
    {
        perror("chdir");
        return false;
    }
    char* modifies = realpath(modifies_path, NULL);

    if (!modifies || !strlen(modifies))
    {
        // fprintf(stderr, "Could not find file to patch at %s/%s\n", repo_directory, modifies_path);
        // return false;
        // This behaviour is invalid, we should make the file path instead.
        char* pwd = realpath(".", NULL);
        size_t path_len = snprintf(NULL, 0, "%s/%s", pwd, modifies_path);
        modifies = malloc(path_len+1);
        snprintf(modifies, path_len + 1, "%s/%s", pwd, modifies_path);
    }

    // printf("Leaving directory %s\n", repo_directory);
    if (chdir("..") == -1)
    {
        perror("chdir");
        return false;
    }

    if (!patch || !strlen(patch))
    {
        fprintf(stderr, "Could not find patch at %s\n", patch_path);
        return false;
    }

    // TODO: Use a library?
    string_array argv = {};
    string_array_append(&argv, "patch");
    string_array_append(&argv, "-u");
    string_array_append(&argv, modifies);
    string_array_append(&argv, "-i");
    string_array_append(&argv, patch);
    string_array_append(&argv, "-t");
    bool res = !run_command(argv.buf[0], argv);

    free(modifies);
    free(patch);
    return res;
}

void remove_recursively(const char* path);
#if ENABLE_GIT
static bool clone_repository(const char* pkg_name, const char* url, const char* hash)
{
    const char* dir_name = strrchr(url, '/')+1;
    string_array argv_rm = {};
    string_array_append(&argv_rm, "rm");
    string_array_append(&argv_rm, "-rf");
    string_array_append(&argv_rm, dir_name);
    run_command("rm", argv_rm);
    string_array_free(&argv_rm);

    // TODO: Use a library?
    string_array argv = {};
    string_array_append(&argv, "git");
    string_array_append(&argv, "clone");
    string_array_append(&argv, url);
    string_array_append(&argv, "-b");
    string_array_append(&argv, hash);
    int ret = run_command("git", argv);
    if (ret != EXIT_SUCCESS)
    {
        printf("Git failed with exit code %d\n", ret);
#ifndef NDEBUG
        printf("Leaving directory %s\n", pkg_name);
#endif
        if (chdir("..") == -1)
        {
            perror("chdir");
            remove_recursively(pkg_name);
            return false;
        }
        remove_recursively(pkg_name);
        return false;
    }

    string_array_free(&argv);
    argv = (string_array){};

    return true;
}
#else
static bool clone_repository(const char *pkg_name, const char* url, const char* hash)
{
    printf("%s: FATAL: Compiled without git support enabled, and git repository package was found.\n", g_argv[0]);
    return false;
}
#endif

static bool fetch(package* pkg, curl_handle curl_hnd)
{
    // Fetch the repository/archive.
#ifndef NDEBUG
    printf("Entering directory %s\n", repo_directory);
#endif
    if (chdir(repo_directory) == -1)
    {
        perror("chdir");
        return false;
    }

    bool fetched = false;

    switch (pkg->source_type)
    {
        case SOURCE_TYPE_WEB:
        {
            char* archive = download_archive(curl_hnd, pkg->source.web.url);
            if (!archive)
            {
                fetched = false;
                break;
            }
            fetched = extract_archive(pkg->source.web.url, pkg->name, archive);
            remove(archive);
            free(archive);
            break;
        }
        case SOURCE_TYPE_GIT:
        {
            fetched = clone_repository(pkg->name, pkg->source.git.git_url, pkg->source.git.git_commit);
            break;
        }
        case SOURCE_TYPE_SOURCELESS:
            fetched = true;
            break;
        default:
            printf("FATAL: Invalid source type: %d. this is a bug, report it.\n", pkg->source_type);
            unlock();
            abort();
    }

    done_fetch:
#ifndef NDEBUG
    printf("Leaving directory %s\n", repo_directory);
#endif
    if (chdir("..") == -1)
    {
        perror("chdir");
        return false;
    }

    return fetched;
}

bool build_pkg_internal(package* pkg, curl_handle curl_hnd, bool install, bool satisfy_dependencies)
{
    if (pkg->host_package && pkg->host_provides)
    {
        // Hopefully this doesn't hang.
        string_array argv = {};
        string_array_append(&argv, pkg->host_provides);
        string_array_append(&argv, "-v");
        int ec = run_command(pkg->host_provides, argv);
        if (!ec)
        {
            // It exists.
            printf("%s provided by host package %s is already installed from an external source. Assuming it works...\n", pkg->host_provides, pkg->name);
            struct pkginfo* info = read_package_info(pkg->name);
            info->build_state = BUILD_STATE_INSTALLED;

            const char *triplet = pkg->host_package ? g_config.host_triplet : g_config.target_triplet;
            info->host_triplet_len = strlen(triplet);
            info = realloc(info, sizeof(*info) + info->host_triplet_len);
            assert(info);
            memcpy(info->host_triplet, triplet, info->host_triplet_len);
            info->cross_compiled = g_config.cross_compiling;

            write_package_info(pkg->name, info);
            return true;
        }
    }

    // Satisfy dependencies.
    for (size_t i = 0; i < pkg->depends.cnt && satisfy_dependencies; i++)
    {
        const char* depend = pkg->depends.buf[i];
//      printf("Looking for dependency %s\n", depend);
        package* depend_pkg = get_package(depend);
        if (!depend_pkg)
        {
            printf("%s: While satisfying dependencies for package %s: Invalid or unknown package '%s'\nAbort.\n", g_argv[0], pkg->name, depend);
            return false;
        }
        // TODO: Make non-recursive?
        struct pkginfo* info = read_package_info(pkg->name);
        if (info->build_state < (BUILD_STATE_BUILT+install))
            printf("Building dependency %s, '%s'\n", depend_pkg->name, depend_pkg->description);
        else
        {
            free(info);
            continue;
        }
        free(info);
        if (!build_pkg_internal(depend_pkg, curl_hnd, install, satisfy_dependencies))
            return false;
    }

    struct pkginfo* info = read_package_info(pkg->name);
    if (info->build_state < BUILD_STATE_FETCHED)
    {
        if (!fetch(pkg, curl_hnd))
        {
            free(info);
            return false;
        }
        for (size_t i = 0; i < pkg->patches.cnt; i++)
        {
            if (pkg->patches.buf[i].delete_file)
            {
                chdir(repo_directory);
                remove(pkg->patches.buf[i].modifies);
                chdir("..");
            }
            if (!apply_patch(pkg->patches.buf[i].patch, pkg->patches.buf[i].modifies))
            {
                free(info);
                return false;
            }
        }
        info->build_state = BUILD_STATE_FETCHED;
        write_package_info(pkg->name, info);
    }

#ifndef NDEBUG
    printf("Entering directory %s\n", bootstrap_directory);
#endif
    if (chdir(bootstrap_directory) == -1)
    {
        perror("chdir");
        free(info);
        return false;
    }

    if (info->build_state < BUILD_STATE_CONFIGURED)
        remove_recursively(pkg->name);
    mkdir(pkg->name, 0777);
    mkdir(package_make_bin_prefix(pkg), 0777);
    
#ifndef NDEBUG
   printf("Entering directory %s\n", pkg->name);
#endif
    if (chdir(pkg->name) == -1)
    {
        perror("chdir");
        free(info);
        return false;
    }

    if (info->build_state < BUILD_STATE_CONFIGURED)
    {
        // Run bootstrap commands.
        command* cmd = NULL;
        int ec = command_array_run(&pkg->bootstrap_commands, &cmd);
        if (ec != EXIT_SUCCESS && cmd)
        {
            printf("%s exited with code %d\n", cmd->proc, ec);
            free(info);
#ifndef NDEBUG
            printf("Leaving directory %s/%s\n", bootstrap_directory, pkg->name);
#endif
            if (chdir("../../") == -1)
            {
                perror("chdir");
                return false;
            }

            return false;
        }

        info->build_state = BUILD_STATE_CONFIGURED;

        const char *triplet = pkg->host_package ? g_config.host_triplet : g_config.target_triplet;
        info->host_triplet_len = strlen(triplet);
        info = realloc(info, sizeof(*info) + info->host_triplet_len);
	assert(info);
        memcpy(info->host_triplet, triplet, info->host_triplet_len);
        info->cross_compiled = g_config.cross_compiling;

        gettimeofday(&info->configure_date, NULL);

        write_package_info(pkg->name, info);
    }

    if (info->build_state < BUILD_STATE_BUILT)
    {
        // Run build commands.
        command* cmd = NULL;
        int ec = command_array_run(&pkg->build_commands, &cmd);
        if (ec != EXIT_SUCCESS && cmd)
        {
            printf("%s exited with code %d\n", cmd->proc, ec);
            free(info);
#ifndef NDEBUG
            printf("Leaving directory %s/%s\n", bootstrap_directory, pkg->name);
#endif
            if (chdir("../../") == -1)
            {
                perror("chdir");
                return false;
            }

            return false;
        }

        info->build_state = BUILD_STATE_BUILT;
        gettimeofday(&info->configure_date, NULL);
        write_package_info(pkg->name, info);
    }

    if (info->build_state < BUILD_STATE_INSTALLED && install)
    {
        // Run install commands.
        command* cmd = NULL;
        int ec = command_array_run(&pkg->install_commands, &cmd);
        if (ec != EXIT_SUCCESS && cmd)
        {
            printf("%s exited with code %d\n", cmd->proc, ec);
            free(info);
#ifndef NDEBUG
            printf("Leaving directory %s/%s\n", bootstrap_directory, pkg->name);
#endif
            if (chdir("../../") == -1)
            {
                perror("chdir");
                return false;
            }

            return false;
        }

        info->build_state = BUILD_STATE_INSTALLED;
        gettimeofday(&info->configure_date, NULL);
        write_package_info(pkg->name, info);
    }

#ifndef NDEBUG
    printf("Leaving directory %s\n", bootstrap_directory);
#endif
    if (chdir("..") == -1)
    {
        perror("chdir");
        free(info);
        return false;
    }

    free(info);

    return true;
}

void build_pkg(const char* name)
{
    lock();
    package* pkg = get_package(name);
    if (!pkg)
    {
        printf("%s: Invalid or unknown package '%s'\nAbort.\n", g_argv[0], name);
        unlock();
        return;
    }
    printf("Building %s, '%s'.\n", pkg->name, pkg->description);
    curl_handle curl_hnd = init_curl();
    if (!curl_hnd)
    {
        printf("curl_easy_init failed\n");
        unlock();
        return;
    }
    build_pkg_internal(pkg, curl_hnd, false, true);
    cleanup_curl(curl_hnd);
    unlock();
}

void install_pkg(const char* name)
{
    lock();
    package* pkg = get_package(name);
    if (!pkg)
    {
        printf("%s: Invalid or unknown package '%s'\nAbort.\n", g_argv[0], name);
        unlock();
        return;
    }
    printf("Building %s, '%s'.\n", pkg->name, pkg->description);
    curl_handle curl_hnd = init_curl();
    if (!curl_hnd)
    {
        printf("curl_easy_init failed\n");
        unlock();
        return;
    }
    build_pkg_internal(pkg, curl_hnd, true, true);
    cleanup_curl(curl_hnd);
    unlock();
}

void run_pkg(const char* name)
{
    lock();
    package* pkg = get_package(name);
    if (!pkg)
    {
        printf("%s: Invalid or unknown package '%s'\nAbort.\n", g_argv[0], name);
        unlock();
        return;
    }
    struct pkginfo* info = read_package_info(name);
    if (info->build_state < BUILD_STATE_INSTALLED)
    {
        curl_handle curl_hnd = init_curl();
        if (!curl_hnd)
        {
            printf("curl_easy_init failed\n");
            unlock();
            return;
        }
        printf("Installing %s, '%s'.\n", pkg->name, pkg->description);
        build_pkg_internal(pkg, curl_hnd, true, true);
        cleanup_curl(curl_hnd);
    }
    free(info);
    printf("Running %s, '%s'.\n", pkg->name, pkg->description);
    command_array_run(&pkg->run_commands, NULL);
    unlock();
}

void rebuild_pkg(const char* name)
{
    lock();
    package* pkg = get_package(name);
    if (!pkg)
    {
        printf("%s: Invalid or unknown package '%s'\nAbort.\n", g_argv[0], name);
        unlock();
        return;
    }
    struct pkginfo* info = read_package_info(name);
    bool install = info->build_state == BUILD_STATE_INSTALLED;
    info->build_state = BUILD_STATE_CLEAN;
    write_package_info(name, info);
    free(info);
    printf("Rebuilding %s, '%s'.\n", pkg->name, pkg->description);
    curl_handle curl_hnd = init_curl();
    if (!curl_hnd)
    {
        printf("curl_easy_init failed\n");
        unlock();
        return;
    }
    build_pkg_internal(pkg, curl_hnd, install, true);
    cleanup_curl(curl_hnd);
    unlock();
}
