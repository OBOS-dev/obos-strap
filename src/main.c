#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

const char* help =
"build, clean, buildall, rebuild,\n"
"setup-env\n";

const char* prefix_directory = "./pkgs";
const char* bootstrap_directory = "./bootstrap";
const char* repo_directory = "./repos";
const char* recipes_directory = "./recipes";

void clean();
void build_pkg(const char* pkg);
void rebuild(const char* pkg) {}

int g_argc = 0;
char** g_argv = 0;

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("%s: %s", argv[0], help);
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
        rebuild(argv[2]);
    }
    else if (strcmp(argv[1], "setup-env") == 0)
    {
        printf("%s: Setting up enviornment\n", argv[0]);
        struct stat st = {};
        if (stat(recipes_directory, &st) == -1)
        {
            printf("%s: Fatal: Could not find recipes directory.\n", argv[0]);
            return -1;
        }
        printf("%x\n", st.st_mode);
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
    }
    else
    {
        printf("%s: %s", argv[0], help);
        return -1;
    }

    return 0;
}
