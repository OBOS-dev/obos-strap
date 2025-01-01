#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lock.h"

#if HAS_LIBCURL
#   include <curl/curl.h>
#endif

const char* help =
"build, clean, buildall, rebuild, setup-env, force-unlock, install, installall, chroot\n";

/*
 * build - DONE
 * clean - DONE
 * buildall - TODO
 * rebuild - DONE
 * setup-env - DONE
 * force-unlock - DONE
 * install - DONE
 * installall - TODO
 * chroot - DONE
 * git repos - TODO
 */

const char* prefix_directory = "./pkgs";
const char* bootstrap_directory = "./bootstrap";
const char* repo_directory = "./repos";
const char* recipes_directory = "./recipes";
const char* pkg_info_directory = "./pkginfo";

void clean();
void build_pkg(const char* pkg);
void rebuild_pkg(const char* pkg);
void install_pkg(const char* pkg);

int g_argc = 0;
char** g_argv = 0;

int main(int argc, char **argv)
{
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
        if (!(st.st_mode & 0400) || !(st.st_mode & 0040) || !(st.st_mode & 0004))
        {
            printf("%s: Fatal: Found recipes directory, but it is unreadable.\n", argv[0]);
            return -1;
        }

        struct stat tmp = {};

        if (stat(prefix_directory, &tmp) == -1)
        {
            if (mkdir(prefix_directory, st.st_mode | 0200) == -1)
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
    pkg_info_directory = realpath(pkg_info_directory, NULL);
    prefix_directory = realpath(prefix_directory, NULL);
    bootstrap_directory = realpath(bootstrap_directory, NULL);
    repo_directory = realpath(repo_directory, NULL);
    recipes_directory = realpath(recipes_directory, NULL);
    if (!recipes_directory)
    {
        printf("FATAL: Recipes directory does not exist.\n");
        return -1;
    }
    if (!pkg_info_directory || !prefix_directory || !bootstrap_directory || !repo_directory)
    {
        printf("One or more required directories are missing. Did you forget to run %s setup-env after cleaning?\n", g_argv[0]);
        return -1;
    }
    g_argc = argc;
    g_argv = argv;
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
            printf("%s build pkg\n", argv[0]);
            return -1;
        }
        install_pkg(argv[2]);
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
                case 'n': puts("Abort\n"); return 1;
                case '\n': break;
                default: fputs("Please put y/n ", stdout); break;
            }
        } while(c != 'y');
        clean();
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
                case 'n': puts("Abort\n"); return 1;
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
        if (chroot(prefix_directory) == -1)
        {
            perror("chroot");
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
