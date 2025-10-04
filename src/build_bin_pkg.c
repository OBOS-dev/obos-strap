/*
 * src/build_bin_pkg.c
 *
 * Copyright (c) 2025 Omar Berrow
 */

#define _DEFAULT_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>

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

static void build_binary_pkg_dependencies(package* pkg);

static void create_pkg(package* pkg, bool build_dependencies)
{
    if (build_dependencies)
        build_binary_pkg_dependencies(pkg);

    (void)package_make_bin_prefix(pkg);

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
    bin_package_path_len = snprintf(NULL,0, "%s/%s-%d.%d.%d.tar", 
        binary_package_directory,
        pkg->name,
        pkg->version.arr[0], pkg->version.arr[1], pkg->version.arr[2]);
    bin_package_path = malloc(bin_package_path_len+1);
    snprintf(bin_package_path, bin_package_path_len+1, "%s/%s-%d.%d.%d.tar", 
        binary_package_directory,
        pkg->name,
        pkg->version.arr[0], pkg->version.arr[1], pkg->version.arr[2]);

    struct stat st = {};
    if (stat(bin_package_path, &st) == 0)
    {
        struct pkginfo* info = read_package_info(pkg->name);
        struct timeval install_time = info->install_date;
        struct timeval mtim = {};
        free(info);
        mtim.tv_sec = st.st_mtim.tv_sec;
        mtim.tv_usec = st.st_mtim.tv_nsec/1000;
        if (timercmp(&mtim, &install_time, >=))
        {
            chdir(root_directory);
            return;
        }
    }

    command_array run_tar = {};
    command tar_cmd = {.proc="tar"};
    command gz_cmd = {.proc="gzip"};
    
    string_array_append(&tar_cmd.argv, "tar");
    string_array_append(&tar_cmd.argv, "-cf");
    string_array_append(&tar_cmd.argv, bin_package_path);
    string_array_append(&tar_cmd.argv, "* >/dev/null 2>&1");

    string_array_append(&gz_cmd.argv, "gzip");
    string_array_append(&gz_cmd.argv, "-qf9");
    string_array_append(&gz_cmd.argv, bin_package_path);

    command_array_append(&run_tar, &tar_cmd);
    command_array_append(&run_tar, &gz_cmd);

    command_array_run(&run_tar, NULL);

    command_array_free(&run_tar);

    free(bin_package_path);

    chdir(root_directory);
}

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

    build_binary_pkg_dependencies(pkg);

    if (!pkg->supports_binary_packages)
    {
        printf("Package does not support binary packaging.\n");
        unlock();
        return;
    }

    create_pkg(pkg, false);
    
    unlock();
}

static void build_binary_pkg_dependencies(package* pkg)
{
    for (size_t i = 0; i < pkg->depends.cnt; i++)
    {
        package* dependency = get_package(pkg->depends.buf[i]);
        if (dependency && dependency->supports_binary_packages)
            create_pkg(dependency, true);
    }
}
