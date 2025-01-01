/*
 * src/build_pkg.c
 *
 * Copyright (c) 2024 Omar Berrow
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>
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
char* curl_err = NULL;

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

static char* download_archive(curl_handle hnd, const char* url, const char* destdir)
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
#endif

static bool build_pkg_impl(package* pkg, curl_handle curl_hnd)
{
    // Satisfy dependencies.
    for (size_t i = 0; i < pkg->depends.cnt; i++)
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
        if (!build_pkg_impl(pkg, curl_hnd))
            return false;
    }
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
            char* archive = download_archive(curl_hnd, pkg->source.web.url, pkg->name);
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
        default:
            printf("FATAL: Invalid source type: %d. this is a bug, report it.\n", pkg->source_type);
            unlock();
            abort();
    }

    printf("Leaving directory %s\n", repo_directory);
    if (chdir("..") == -1)
    {
        perror("chdir");
        return false;
    }

    if (!fetched)
        return false;

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

    // Run bootstrap commands.
    for (size_t i = 0; i < pkg->bootstrap_commands.cnt; i++)
    {
        command* cmd = &pkg->bootstrap_commands.buf[i];
        run_command(cmd->proc, cmd->argv);
    }

    // Run build commands.
    for (size_t i = 0; i < pkg->build_commands.cnt; i++)
    {
        command* cmd = &pkg->build_commands.buf[i];
        run_command(cmd->proc, cmd->argv);
    }

    printf("Leaving directory %s\n", bootstrap_directory);
    if (chdir("..") == -1)
    {
        perror("chdir");
        return false;
    }

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
    build_pkg_impl(pkg, curl_hnd);
    cleanup_curl(curl_hnd);
    unlock();
}
