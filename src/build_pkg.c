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
static bool download_archive(curl_handle hnd, const char* url, const char* destdir)
{
    (void)(hnd);
    (void)(destdir);
    printf("Could not download archive %s. You must build obos-strap with libcurl installed.\n", url);
    return false;
}
static bool extract_archive(const char* url, const char* name, char* archive_path)
{
    (void)(url);
    (void)(name);
    (void)(archive_path);
}
#endif

// Applies a patch 'patch_path' to the file 'modifies_path'
// This is run in the initial CWD of the process.
static bool apply_patch(const char* patch_path, const char* modifies_path)
{
    char* patch = realpath(patch_path, NULL);
    printf("Entering directory %s\n", repo_directory);
    if (chdir(repo_directory) == -1)
    {
        perror("chdir");
        return false;
    }
    char* modifies = realpath(modifies_path, NULL);
    printf("Leaving directory %s\n", repo_directory);
    if (chdir("..") == -1)
    {
        perror("chdir");
        return false;
    }

    // TODO: Use a library?
    string_array argv = {};
    string_array_append(&argv, "patch");
    string_array_append(&argv, "-u");
    string_array_append(&argv, modifies);
    string_array_append(&argv, "-i");
    string_array_append(&argv, patch);
    bool res = run_command(argv.buf[0], argv);

    free(modifies);
    free(patch);
    return res;
}

#if ENABLE_GIT
void remove_recursively(const char* path);
static bool clone_repository(const char* pkg_name, const char* url, const char* hash)
{
    struct stat st = {};
    if (stat(pkg_name, &st))
        remove_recursively(pkg_name);

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
        printf("Leaving directory %s\n", pkg_name);
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
    return false;
}
#endif

static bool fetch(package* pkg, curl_handle curl_hnd)
{
    // Fetch the repository/archive.
    printf("Entering directory %s\n", repo_directory);
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
        default:
            printf("FATAL: Invalid source type: %d. this is a bug, report it.\n", pkg->source_type);
            unlock();
            abort();
    }

    done_fetch:
    printf("Leaving directory %s\n", repo_directory);
    if (chdir("..") == -1)
    {
        perror("chdir");
        return false;
    }

    return fetched;
}

bool build_pkg_internal(package* pkg, curl_handle curl_hnd, bool install, bool satisfy_dependencies)
{
    // Satisfy dependencies.
    for (size_t i = 0; i < pkg->depends.cnt && satisfy_dependencies; i++)
    {
        const char* depend = pkg->depends.buf[i];
        printf("Looking for dependency %s\n", depend);
        package* depend_pkg = get_package(depend);
        if (!depend_pkg)
        {
            printf("%s: While satisfying dependencies for package %s: Invalid or unknown package '%s'\nAbort.\n", g_argv[0], pkg->name, depend);
            return false;
        }
        printf("Building dependency %s, '%s'\n", depend_pkg->name, depend_pkg->description);
        // TODO: Make non-recursive?
        if (!build_pkg_internal(pkg, curl_hnd, install, satisfy_dependencies))
            return false;
    }

    struct pkginfo* info = read_package_info(pkg->name);
    if (info->build_state < BUILD_STATE_FETCHED)
    {
        if (!fetch(pkg, curl_hnd))
            return false;
        for (size_t i = 0; i < pkg->patches.cnt; i++)
            apply_patch(pkg->patches.buf[i].patch, pkg->patches.buf[i].modifies);
        info->build_state = BUILD_STATE_FETCHED;
        write_package_info(pkg->name, info);
    }

    printf("Entering directory %s\n", bootstrap_directory);
    if (chdir(bootstrap_directory) == -1)
    {
        perror("chdir");
        return false;
    }

    struct stat st = {};
    if (stat(pkg->name, &st) == -1)
        mkdir(pkg->name, 0777);
    int dir_fd = open(pkg->name, O_DIRECTORY);
    printf("Entering directory %s\n", pkg->name);
    if (fchdir(dir_fd) == -1)
    {
        perror("chdir");
        return false;
    }

    if (info->build_state < BUILD_STATE_CONFIGURED)
    {
        // Run bootstrap commands.
        for (size_t i = 0; i < pkg->bootstrap_commands.cnt; i++)
        {
            command* cmd = &pkg->bootstrap_commands.buf[i];
            int ec = run_command(cmd->proc, cmd->argv);
            if (ec != EXIT_SUCCESS)
            {
                printf("%s exited with code %d\n", cmd->proc, ec);
                free(info);
                printf("Leaving directory %s/%s\n", bootstrap_directory, pkg->name);
                if (chdir("../../") == -1)
                {
                    perror("chdir");
                    return false;
                }

                return false;
            }
        }
        info->build_state = BUILD_STATE_CONFIGURED;
        gettimeofday(&info->configure_date, NULL);
        write_package_info(pkg->name, info);
    }

    if (info->build_state < BUILD_STATE_BUILT)
    {
        // Run build commands.
        for (size_t i = 0; i < pkg->build_commands.cnt; i++)
        {
            command* cmd = &pkg->build_commands.buf[i];
            int ec = run_command(cmd->proc, cmd->argv);
            if (ec != EXIT_SUCCESS)
            {
                printf("%s exited with code %d\n", cmd->proc, ec);
                free(info);
                printf("Leaving directory %s/%s\n", bootstrap_directory, pkg->name);
                if (chdir("../../") == -1)
                {
                    perror("chdir");
                    return false;
                }

                return false;
            }
        }
        info->build_state = BUILD_STATE_BUILT;
        gettimeofday(&info->configure_date, NULL);
        write_package_info(pkg->name, info);
    }

    if (info->build_state < BUILD_STATE_INSTALLED && install)
    {
        // Run install commands.
        for (size_t i = 0; i < pkg->install_commands.cnt; i++)
        {
            command* cmd = &pkg->install_commands.buf[i];
            int ec = run_command(cmd->proc, cmd->argv);
            if (ec != EXIT_SUCCESS)
            {
                printf("%s exited with code %d\n", cmd->proc, ec);
                free(info);
                printf("Leaving directory %s\n", bootstrap_directory);
                if (chdir("..") == -1)
                {
                    perror("chdir");
                    return false;
                }

                return false;
            }
        }
        info->build_state = BUILD_STATE_INSTALLED;
        gettimeofday(&info->configure_date, NULL);
        write_package_info(pkg->name, info);
    }

    printf("Leaving directory %s\n", bootstrap_directory);
    if (chdir("..") == -1)
    {
        perror("chdir");
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
