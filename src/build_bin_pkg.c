/*
 * src/build_bin_pkg.c
 *
 * Copyright (c) 2025 Omar Berrow
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "package.h"
#include "lock.h"
#include "path.h"

#if HAS_LIBCURL
#include <curl/curl.h>
#include <curl/easy.h>

typedef CURL *curl_handle;

#define cleanup_curl curl_easy_cleanup
extern curl_handle init_curl();
#else
#define init_curl() (-1)
#define cleanup_curl(c) (void)(c)
typedef int curl_handle;
#endif

extern bool build_pkg_internal(package* pkg, curl_handle curl_hnd, bool install, bool satisfy_dependencies);
void build_binary_package(const char* name)
{
    lock();
    package* pkg = get_package(name);
    if (!pkg)
    {
        printf("%s: Invalid or unknown package '%s'\nAbort.\n", g_argv[0], name);
        unlock();
        return;
    }
    if (pkg->host_package)
    {
        printf("%s: Refusing to build binary package for host package %s.\n", g_argv[0], pkg->name);
        unlock();
        return;
    }
    printf("Building binary package for %s.\n", pkg->name);
    curl_handle curl_hnd = init_curl();
    if (!curl_hnd)
    {
        printf("curl_easy_init failed\n");
        unlock();
        return;
    }
    build_pkg_internal(pkg, curl_hnd, true, true);
    cleanup_curl(curl_hnd);

    if (!pkg->supports_binary_packages)
    {
        printf("Package does not support binary packaging.\n");
        unlock();
        return;
    }

#ifndef NDEBUG
    printf("Entering directory %s\n", pkg->bin_package_prefix);
#endif
    if (chdir(pkg->bin_package_prefix) == -1)
    {
        unlock();
        perror("chdir");
        return;
    }

    char *bin_package_path = NULL;
    size_t bin_package_path_len = 0;
    bin_package_path_len = snprintf(NULL,0, "%s/%s.tar", binary_package_directory, pkg->name);
    bin_package_path = malloc(bin_package_path_len+1);
    snprintf(bin_package_path, bin_package_path_len+1, "%s/%s.tar", binary_package_directory, pkg->name);

    command_array run_tar = {};
    command tar_cmd = {.proc="tar"};
    command gz_cmd = {.proc="gzip"};
    
    string_array_append(&tar_cmd.argv, "tar");
    string_array_append(&tar_cmd.argv, "-cf");
    string_array_append(&tar_cmd.argv, bin_package_path);
    string_array_append(&tar_cmd.argv, "*");

    string_array_append(&gz_cmd.argv, "gzip");
    string_array_append(&gz_cmd.argv, bin_package_path);

    command_array_append(&run_tar, &tar_cmd);
    command_array_append(&run_tar, &gz_cmd);

    command_array_run(&run_tar, NULL);

    command_array_free(&run_tar);

    free(bin_package_path);
    
    unlock();
}