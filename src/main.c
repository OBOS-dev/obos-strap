/*
 * src/main.c
 *
 * Copyright (c) 2024-2025 Omar Berrow
 */

#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <errno.h>
#include <strings.h>
#include <cjson/cJSON.h>
#include <time.h>

#include "lock.h"
#include "package.h"
#include "path.h"
#include "update.h"

#if HAS_LIBCURL
#   include <curl/curl.h>
#endif

/*
 * build - DONE
 * clean - DONE
 * buildall - DONE
 * rebuild - DONE
 * setup-env - DONE
 * force-unlock - DONE
 * install - DONE
 * installall - DONE
 * chroot - DONE
 * git repos - DONE
 */

const char* destination_directory = "./pkgs";
const char* prefix_directory = "/usr"; // Sensible default.
const char* host_prefix_directory = "./host_pkgs";
const char* root_directory = ".";
const char* binary_package_directory = "./bin_pkgs/";
const char* bootstrap_directory = "./bootstrap";
const char* repo_directory = "./repos";
const char* recipes_directory = "./recipes";
const char* pkg_info_directory = "./pkginfo";

void clean();
void build_pkg(const char* pkg);
void rebuild_pkg(const char* pkg);
void install_pkg(const char* pkg);
void build_binary_package(const char* name);
void run_pkg(const char* pkg);
void buildall();

int g_argc = 0;
char** g_argv = 0;

const char* help =
"build, clean, build-all/install-all, rebuild, setup-env, force-unlock, install, chroot, run, list, update, install-bin-pkg, outdated, start-proc\n";

const char* version =
"obos-strap v0.0.1\n"
"Copyright (c) 2024-2025 Omar Berrow\n\n"
"From OpenBSD (sys/tree.h)\n"
"Copyright 2002 Niels Provos <provos@citi.umich.edu>\n\
All rights reserved.\n\
\n\
Redistribution and use in source and binary forms, with or without\n\
modification, are permitted provided that the following conditions\n\
are met:\n\
1. Redistributions of source code must retain the above copyright\n\
   notice, this list of conditions and the following disclaimer.\n\
2. Redistributions in binary form must reproduce the above copyright\n\
   notice, this list of conditions and the following disclaimer in the\n\
   documentation and/or other materials provided with the distribution.\n\
\n\
THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR\n\
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES\n\
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.\n\
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,\n\
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT\n\
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,\n\
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY\n\
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT\n\
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF\n\
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n"
;

struct config g_config;

struct list_cb_user {
    // For each package found, the entry in the packages
    // array is set to nullptr
    // If nPackages is zero, then all packages are listed.
    const char** packages;
    size_t nPackages;
    size_t nPackagesListed;
    bool verbose : 1;
    bool installed_only : 1;
};
int list_cb(package* pkg, struct pkginfo* info, void* userdata)
{
    struct list_cb_user* opt = userdata;
    if (!opt->verbose)
        printf("%s\n", pkg->name);
    else if (info)
    {
        static char const* const build_state_strs[] = {
            "CLEAN",
            "FETCHED",
            "CONFIGURED",
            "BUILT",
            "INSTALLED",
        };
        static char const* const weekdays[] = {
            "Sunday",
            "Monday",
            "Tuesday",
            "Wednesday",
            "Thursday",
            "Friday",
            "Saturday",
        };
        static char const* const months[] = {
            "January",
            "Febuary",
            "March",
            "April",
            "May",
            "June",
            "July",
            "August",
            "September",
            "October",
            "November",
            "December",
        };
        struct tm* tm_info = NULL;
        if (info->build_state > BUILD_STATE_CONFIGURED && info->build_state <= BUILD_STATE_INSTALLED)
            tm_info = localtime(&info->build_times[info->build_state-BUILD_STATE_CONFIGURED].tv_sec);
        if (tm_info)
        {
            printf("%s@%d.%d.%d-%.*s BUILD_STATE_%s since %s, %s %02d, %d at %02d:%02d:%02d\n",
                pkg->name,
                info->version.major, info->version.minor,
                info->version.patch,
                (int)info->host_triplet_len, info->host_triplet,
                info->build_state < (sizeof(build_state_strs)/sizeof(*build_state_strs)) ? 
                    build_state_strs[info->build_state] :
                    "INVALID",
                weekdays[tm_info->tm_wday],
                months[tm_info->tm_mon],
                tm_info->tm_mday,
                tm_info->tm_year+1900,
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec
            );
        }
        else
        {
            printf("%s@%d.%d.%d-%.*s BUILD_STATE_%s\n",
                pkg->name,
                info->version.major, info->version.minor,
                info->version.patch,
                (int)info->host_triplet_len, info->host_triplet,
                info->build_state < (sizeof(build_state_strs)/sizeof(*build_state_strs)) ? 
                    build_state_strs[info->build_state] :
                    "INVALID"
            );
        }
    }
    else
        printf("%s@%d.%d.%d-%s BUILD_STATE_CLEAN\n", 
            pkg->name, 
            pkg->version.major, pkg->version.minor, pkg->version.patch,
            pkg->host_package || !g_config.cross_compiling ? g_config.host_triplet : g_config.target_triplet
        );
    free(info);
    free(pkg);
    return 0;
}

int main(int argc, char **argv)
{
    argv[0] = basename(argv[0]);
    if (argc < 2)
    {
        printf("%s: %s", argv[0], help);
        return -1;
    }

#if HAS_LIBCURL
    int curl_ec = 0;
    if ((curl_ec = curl_global_init(CURL_GLOBAL_DEFAULT)))
    {
        printf("curl_global_init: %d", curl_ec);
        return -1;
    }
#endif

    if (strcmp(argv[1], "setup-env") == 0)
    {
        printf("%s: Setting up enviornment\n", argv[0]);
        struct stat st = {};
        if (stat(recipes_directory, &st) == -1)
        {
            printf("%s: Fatal: Could not find recipes directory.\n", argv[0]);
            return -1;
        }
        if (!(st.st_mode & 0100) || !(st.st_mode & 0010) || !(st.st_mode & 0001))
        {
            printf("%s: Fatal: Found recipes directory, but it is unreadable.\n", argv[0]);
            return -1;
        }

        struct stat tmp = {};

        if (stat(destination_directory, &tmp) == -1)
        {
            if (mkdir(destination_directory, st.st_mode | 0200) == -1)
            {
                perror("mkdir");
                return -1;
            }
        }

        if (stat(host_prefix_directory, &tmp) == -1)
        {
            if (mkdir(host_prefix_directory, st.st_mode | 0200) == -1)
            {
                perror("mkdir");
                return -1;
            }
        }
        if (stat(binary_package_directory, &tmp) == -1)
        {
            if (mkdir(binary_package_directory, st.st_mode | 0200) == -1)
            {
                perror("mkdir");
                return -1;
            }
        }

        if (stat(pkg_info_directory, &tmp) == -1)
        {
            if (mkdir(pkg_info_directory, st.st_mode | 0200) == -1)
            {
                perror("mkdir");
                return -1;
            }
            static const char readme_contents[] =
            "Modifying any of the contents of the files in this directory can be fatal, and require you to rebuild all the packages!\n"
            "Leave, unless you know what you are doing.\n";
                char* readme_path = NULL;
                size_t pathlen = snprintf(NULL, 0, "%s/README", pkg_info_directory);
                readme_path = malloc(pathlen+1);
                snprintf(readme_path, pathlen+1, "%s/README", pkg_info_directory);
                FILE* readme = fopen(readme_path, "w");
                free(readme_path);
                if (readme)
                {
                    fwrite(readme_contents, sizeof(readme_contents[0]), sizeof(readme_contents), readme);
                    fclose(readme);
                }
        }

        if (stat(bootstrap_directory, &tmp) == -1)
        {
            if (mkdir(bootstrap_directory, st.st_mode | 0200) == -1)
            {
                perror("mkdir");
                return -1;
            }
        }

        if (stat(repo_directory, &tmp) == -1)
        {
            if (mkdir(repo_directory, st.st_mode | 0200) == -1)
            {
                perror("mkdir");
                return -1;
            }
        }

        return 0;
    }
    else if(strcmp(argv[1], "version") == 0)
    {
        printf("%s", version);
        return 0;
    }

    g_argc = argc;
    g_argv = argv;

    FILE* pkg_json = fopen("settings.json", "r");
    if (!pkg_json)
    {
        perror("fopen");
        return -1;
    }

    struct stat st = {};
    fstat(fileno(pkg_json), &st);

    char* json_data = malloc(st.st_size);
    fread(json_data, st.st_size, 1, pkg_json);

    fclose(pkg_json);

    cJSON* context = cJSON_Parse(json_data);
    if (!context)
    {
        printf("%s: Parsing settings.json failed.\n", g_argv[0]);
        free(json_data);
        return -1;
    }

    cJSON* destination_override = cJSON_GetObjectItem(context, "destination-override");
    if (!cJSON_IsString(destination_override))
        destination_override = NULL;
    cJSON* prefix_override = cJSON_GetObjectItem(context, "prefix-override");
    if (!cJSON_IsString(prefix_override))
        prefix_override = NULL;
    cJSON* host_prefix_override = cJSON_GetObjectItem(context, "host-prefix-override");
    if (!cJSON_IsString(host_prefix_override))
        host_prefix_override = NULL;
    root_directory = realpath(root_directory, NULL);
    pkg_info_directory = realpath(pkg_info_directory, NULL);
    destination_directory = realpath(destination_override ? cJSON_GetStringValue(destination_override) : destination_directory, NULL);
    if (prefix_override)
        prefix_directory = cJSON_GetStringValue(prefix_override);
    host_prefix_directory = realpath(host_prefix_override ? cJSON_GetStringValue(host_prefix_override) : host_prefix_directory, NULL);
    binary_package_directory = realpath(binary_package_directory, NULL);
    bootstrap_directory = realpath(bootstrap_directory, NULL);
    repo_directory = realpath(repo_directory, NULL);
    recipes_directory = realpath(recipes_directory, NULL);
    if (!recipes_directory)
    {
        printf("FATAL: Recipes directory does not exist.\n");
        return -1;
    }
    if (!pkg_info_directory || !destination_directory || !bootstrap_directory || !repo_directory || !host_prefix_directory || !binary_package_directory)
    {
        printf("One or more required directories are missing. Did you forget to run %s setup-env after cleaning?\n", g_argv[0]);
        return -1;
    }

    do {
        const char* old_path = getenv("PATH");
        assert(old_path);
        size_t len_new_path = snprintf(NULL, 0, "%s/bin:%s", host_prefix_directory, old_path);
        char* new_path = malloc(len_new_path+1);
        snprintf(new_path, len_new_path+1, "%s/bin:%s", host_prefix_directory, old_path);
        setenv("PATH", new_path, 1);
        free(new_path);
    } while(0);

    do {
        size_t len_new_path = snprintf(NULL, 0, "%s/lib/pkgconfig", destination_directory);
        char* new_path = malloc(len_new_path+1);
        snprintf(new_path, len_new_path+1, "%s/lib/pkgconfig", destination_directory);
        setenv("PKG_CONFIG_PATH", new_path, 1);
        free(new_path);
    } while(0);

extern const char* get_str_field_subst(cJSON* parent, const char* fieldname, package* pkg);
    do {
        cJSON* child = cJSON_GetObjectItem(context, "environment");
        if (!child)
            break;
        cJSON *i = NULL;
        cJSON_ArrayForEach(i, child)
        {
            const char* env = get_str_field_subst(i, "env", NULL);
            const char* val = get_str_field_subst(i, "value", NULL);
            if (!env)
            {
                fprintf(stderr, "Invalid environment object in settings.json\n");
                continue;
            }
            if (!val)
            {
                unsetenv(env);
                continue;
            }
            cJSON* replace_obj = cJSON_GetObjectItem(i, "replace");
            bool replace = true;
            if (replace_obj && cJSON_IsBool(replace_obj))
                replace = cJSON_IsTrue(replace_obj);
            if (setenv(env, val, replace) == -1)
                perror("Warning: Could not setenv");            
        }
    } while(0);

    cJSON* child = cJSON_GetObjectItem(context, "cross-compile");
    g_config.cross_compiling = child ? !!cJSON_GetNumberValue(child) : false;
    child = cJSON_GetObjectItem(context, "binary-packages-default");
    g_config.binary_packages_default = child ? !!cJSON_GetNumberValue(child) : false;
    g_config.host_triplet = OBOS_STRAP_HOST_TRIPLET;
    if (g_config.cross_compiling)
    {
        child = cJSON_GetObjectItem(context, "target-triplet");
        g_config.target_triplet = cJSON_GetStringValue(child);
        if (g_config.target_triplet)
            g_config.cross_compiling = strcmp(g_config.host_triplet, g_config.target_triplet) != 0;
        else
        {
            printf("%s: cross-compiling is set to 1, but target-triplet is missing or invalid.\n", g_argv[0]);
            abort();
        }
        // TODO: Verify the target triplet?
    }
    else
        g_config.target_triplet = OBOS_STRAP_HOST_TRIPLET;

    if (strcmp(argv[1], "build") == 0)
    {
        if (argc < 3)
        {
            printf("%s build pkg\n", argv[0]);
            return -1;
        }
        build_pkg(argv[2]);
    }
    else if (strcmp(argv[1], "install") == 0)
    {
        if (argc < 3)
        {
            printf("%s install pkg [build binary package:0/1]\n", argv[0]);
            return -1;
        }
        bool should_build_binary_package = g_config.binary_packages_default;
        if (argc >= 4)
            should_build_binary_package = atoi(argv[3]);
        if (!should_build_binary_package)
            install_pkg(argv[2]);
        else
            build_binary_package(argv[2]);
    }
    else if (strcmp(argv[1], "install-bin-pkg") == 0)
    {
        if (argc < 3)
        {
            printf("%s install-bin-pkg pkg\n", argv[0]);
            return -1;
        }
        build_binary_package(argv[2]);
    }
    else if (strcmp(argv[1], "outdated") == 0)
    {
        if (argc < 3)
        {
            printf("%s outdated pkg\n", argv[0]);
            return -1;
        }
        return !package_outdated(get_package(argv[2]), NULL, BUILD_STATE_INSTALLED);    
    }
    else if (strcmp(argv[1], "update") == 0)
        update();
    else if (strcmp(argv[1], "run") == 0)
    {
        if (argc < 3)
        {
            printf("%s run pkg\n", argv[0]);
            return -1;
        }
        run_pkg(argv[2]);
    }
    else if (strcmp(argv[1], "build-all") == 0 || strcmp(argv[1], "install-all") == 0)
    {
        printf("%s: Installing all packages. This can take a long time.\nContinue? y/n ", argv[0]);
        char c = 0;
        do {
            c = getchar();
            switch (c)
            {
                case 'y': break;
                case 'n': puts("Abort"); return 1;
                case '\n': break;
                default: fputs("Please put y/n ", stdout); break;
            }
        } while(c != 'y');
        buildall();
    }
    else if (strcmp(argv[1], "clean") == 0)
    {
        printf("%s: Cleaning build directory.\nContinue? y/n ", argv[0]);
        char c = 0;
        do {
            c = getchar();
            switch (c)
            {
                case 'y': break;
                case 'n': puts("Abort"); return 1;
                case '\n': break;
                default: fputs("Please put y/n ", stdout); break;
            }
        } while(c != 'y');
        clean();
    }
    else if (strcmp(argv[1], "list") == 0)
    {
        struct list_cb_user opts = {};
        for (size_t i = 2; i < (size_t)argc; i++)
        {
            const char* opt_str = argv[i];
            const char* opt_key = NULL;
            const char* opt_val_str = NULL;
            bool free_opt_key = false;
            const char* equal_sign = strchr(opt_str, '=');
            if (equal_sign)
            {
                opt_val_str = equal_sign+1;
                size_t sz_key = (equal_sign - opt_str);
                opt_key = memcpy(malloc(sz_key + 1), opt_str, sz_key);
                free_opt_key = true;
            }
            else 
            {
                free_opt_key = false;
                opt_key = opt_str;
                opt_val_str = NULL;
            }
            long opt_val_int = 0;
            bool opt_val_bool_valid = true;
            bool opt_val_bool = false;
            if (opt_val_str)
            {
                errno = 0;
                opt_val_int = strtol(opt_val_str, NULL, 0);
                if (errno != 0)
                    opt_val_int = LONG_MAX;
                opt_val_bool = 
                    strcasecmp(opt_val_str, "true") == 0 ? 
                        true : strcasecmp(opt_val_str, "false") ? 
                            false : (opt_val_int == LONG_MAX ? (opt_val_bool_valid = false) : !!opt_val_int);
            }
            else
                opt_val_bool = opt_val_bool_valid = true;

            if (strcasecmp(opt_key, "verbose") == 0)
            {
                if (!opt_val_bool_valid)
                {
                    printf("Invalid value to '%s'. Expected boolean, got %s\n", opt_key, opt_val_str);
                    return -1;
                }
                opts.verbose = opt_val_bool;
            }
            else if (strcasecmp(opt_key, "installed-only") == 0)
            {
                if (!opt_val_bool_valid)
                {
                    printf("Invalid value to '%s'. Expected boolean, got %s\n", opt_key, opt_val_str);
                    return -1;
                }
                opts.installed_only = opt_val_bool;
            }
            else
            {
                // Probably a package to list.
                opts.packages = realloc(opts.packages, (++opts.nPackages) * sizeof(const char*));
                opts.packages[opts.nPackages - 1] = opt_key;
                free_opt_key = false;
            }
            if (free_opt_key)
                free((char*)opt_key);
        }
        if (!opts.nPackages)
            foreach_package(opts.installed_only, list_cb, &opts);
        else
        {
            for (size_t i = 0; i < opts.nPackages; i++)
            {
                package* pkg = get_package(opts.packages[i]);
                if (!pkg)
                {
                    fprintf(stderr, "Could not find package '%s'\n", opts.packages[i]);
                    continue;
                }
                struct pkginfo* info = read_package_info_ex(pkg->name, false, false);
                if (!info && opts.installed_only)
                {
                    free(pkg);
                    continue;
                }
                if (list_cb(pkg, info, &opts) != 0)
                    break;
            }
        }
    }   
    else if (strcmp(argv[1], "rebuild") == 0)
    {
        if (argc < 3)
        {
            printf("%s rebuild pkg\n", argv[0]);
            return -1;
        }
        printf("%s: Rebuilding package %s.\nContinue? y/n ", argv[0], argv[2]);
        char c = 0;
        do {
            c = getchar();
            switch (c)
            {
                case 'y': break;
                case 'n': puts("Abort"); return 1;
                case '\n': break;
                default: fputs("Please put y/n ", stdout); break;
            }
        } while(c != 'y');
        rebuild_pkg(argv[2]);
    }
    else if (strcmp(argv[1], "force-unlock") == 0)
    {
        unlock_forced();
    }
    else if (strcmp(argv[1], "chroot") == 0)
    {
        if (argc < 3)
        {
            printf("%s chroot cmd [args...]\n", argv[0]);
            return -1;
        }
        if (chroot(destination_directory) == -1)
        {
            perror("chroot");
            return -1;
        }
        execvp(argv[2], &argv[2]);
        perror("execvp");
        return -1;
    }
    else if (strcmp(argv[1], "start-proc") == 0)
    {
        if (argc < 3)
        {
            printf("%s start-proc cmd [args...]\n", argv[0]);
            return -1;
        }
        execvp(argv[2], &argv[2]);
        perror("execvp");
        return -1;
    }
    else
    {
        printf("%s: %s", argv[0], help);
        return -1;
    }

    return 0;
}
