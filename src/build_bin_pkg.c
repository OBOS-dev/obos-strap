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
#include <string.h>

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

    if (!pkg->supports_binary_packages)
        return;

    (void)package_make_bin_prefix(pkg);

    struct stat st = {};
    if (stat(pkg->bin_package_prefix, &st) == -1)
    {
        unlock();
        perror("stat");
        return;
    }
    if (!S_ISDIR(st.st_mode))
    {
        unlock();
        fprintf(stderr, "%s is not a directory\n", pkg->bin_package_prefix);
        return;
    }
#ifndef NDEBUG
    printf("Entering directory %s\n", binary_package_directory);
#endif
    if (chdir(binary_package_directory) == -1)
    {
        unlock();
        perror("chdir");
        return;
    }

    command xbps_create_cmd = {.proc="xbps-create"};
    command xbps_rindex_cmd = {.proc="xbps-rindex"};
    
    const char* triplet = pkg->host_package ? g_config.host_triplet : g_config.target_triplet;
    size_t len_arch = strchr(triplet, '-') - triplet;
    char* architecture = memcpy(malloc(len_arch+1), triplet, len_arch);
    architecture[len_arch] = 0;
    size_t package_version_len = snprintf(NULL, 0, "%s-%d.%d_%d", pkg->name, pkg->version.major, pkg->version.minor, pkg->version.patch);
    char* package_version = malloc(package_version_len+1);
    snprintf(package_version, package_version_len+1, "%s-%d.%d_%d", pkg->name, pkg->version.major, pkg->version.minor, pkg->version.patch);
    
    size_t len_package_filename = snprintf(NULL, 0, "%s.%s.xbps", package_version, architecture);
    char* package_filename = malloc(len_package_filename+1);
    snprintf(package_filename, len_package_filename+1, "%s.%s.xbps", package_version, architecture);
    if (stat(package_filename, &st) == 0)
    {
        free(architecture);
        free(package_version);
        free(package_filename);
        return;
    }
    
    size_t depends_str_len = 0;
    for (size_t i = 0; i < pkg->depends.cnt; i++)
    {
        union package_version version;
        char* depend_expr = pkg->depends.buf[i];
        char* depend = NULL;
        int how_cmp = 0;
        parse_depend_expr(depend_expr, &depend, &version, &how_cmp);
        if (!depend)
        {
            fprintf(stderr, "%s: Invalid dependency expression %s in package %s while building binary package. Aborting\n", g_argv[0], depend_expr, pkg->name);
            unlock();
            return;
        }
        package* depend_pkg = get_package(depend);
        if (!depend_pkg)
        {
            fprintf(stderr, "%s: Missing/invalid dependency %s in package %s while building binary package. Aborting\n", g_argv[0], depend_expr, pkg->name);
            unlock();
            return;
        }
        if (!do_version_cmp(how_cmp, depend_pkg->version, version))
        {
            fprintf(stderr, "%s: Could not satisfy dependency. Requires: %s, got: %s=%d.%d.%d. Aborting\n",
                g_argv[0],
                depend_expr,
                depend_pkg->name,
                depend_pkg->version.major, depend_pkg->version.minor, depend_pkg->version.patch
            );
            unlock();
            return;
        }
        if (depend_pkg->supports_binary_packages)
            depends_str_len += strlen(depend_expr)+1;
        free(depend_pkg);
    }
    if (depends_str_len)
        depends_str_len++;
    char* depends_str = depends_str_len ? malloc(depends_str_len) : NULL;
    if (depends_str)
    {
        depends_str[0] = 0;
        for (size_t i = 0; i < pkg->depends.cnt; i++)
        {
            union package_version version;
            char* depend_expr = pkg->depends.buf[i];
            char* depend = NULL;
            int how_cmp = 0;
            parse_depend_expr(depend_expr, &depend, &version, &how_cmp);
            if (!depend)
            {
                fprintf(stderr, "%s: Invalid dependency expression %s in package %s while building binary package. Aborting\n", g_argv[0], depend_expr, pkg->name);
                unlock();
                return;
            }
            package* depend_pkg = get_package(depend);
            if (depend_pkg->supports_binary_packages)
            {
                strcat(depends_str, depend_expr);
                strcat(depends_str, " ");
            }
            free(depend_pkg);
        }
        depends_str[depends_str_len-1] = 0;
    }

    string_array_append(&xbps_create_cmd.argv, xbps_create_cmd.proc);
    if (depends_str)
    {
        string_array_append(&xbps_create_cmd.argv, "-D");
        string_array_append(&xbps_create_cmd.argv, depends_str);
    }
    string_array_append(&xbps_create_cmd.argv, "-q");
    string_array_append(&xbps_create_cmd.argv, "-A");
    string_array_append(&xbps_create_cmd.argv, architecture);
    string_array_append(&xbps_create_cmd.argv, "-n");
    string_array_append(&xbps_create_cmd.argv, package_version);
    string_array_append(&xbps_create_cmd.argv, "-s");
    string_array_append(&xbps_create_cmd.argv, pkg->description);
    string_array_append(&xbps_create_cmd.argv, pkg->bin_package_prefix);

    free(architecture);
    free(package_version);
    free(depends_str);

    int cmd_ret = run_command(xbps_create_cmd.proc, xbps_create_cmd.argv);

    if (cmd_ret != 0)
    {
        fprintf(stderr, "%s returned %d\n", xbps_create_cmd.proc, cmd_ret);
        goto done;
    }

    string_array_append(&xbps_rindex_cmd.argv, xbps_rindex_cmd.proc);
    string_array_append(&xbps_rindex_cmd.argv, "-f");
    string_array_append(&xbps_rindex_cmd.argv, "-a");
    string_array_append(&xbps_rindex_cmd.argv, package_filename);

    cmd_ret = run_command(xbps_rindex_cmd.proc, xbps_rindex_cmd.argv);
    
    if (cmd_ret != 0)
    {
        fprintf(stderr, "%s returned %d\n", xbps_create_cmd.proc, cmd_ret);
        goto done;
    }

    free(package_filename);

    done:
#ifndef NDEBUG
    printf("Entering directory %s\n", root_directory);
#endif
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
        if (dependency)
            create_pkg(dependency, true);
    }
}
