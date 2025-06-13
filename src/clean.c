/*
 * src/clean.c
 *
 * Copyright (c) 2024-2025 Omar Berrow
 */

#define _XOPEN_SOURCE 500

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ftw.h>
#include <unistd.h>

#include "path.h"
#include "package.h"

void remove_recursively(const char* path)
{
    string_array argv = {};
    string_array_append(&argv, "rm");
    string_array_append(&argv, "-rf");
    string_array_append(&argv, path);
    run_command("rm", argv);
    string_array_free(&argv);
}

void clean()
{
    remove_recursively(pkg_info_directory);
    remove_recursively(bootstrap_directory);
    remove_recursively(prefix_directory);
    remove_recursively(host_prefix_directory);
    remove_recursively(repo_directory);
    if (mkdir(prefix_directory, 0755) == -1)
    {
        perror("mkdir");
        return;
    }
    if (mkdir(host_prefix_directory, 0755) == -1)
    {
        perror("mkdir");
        return;
    }
    if (mkdir(pkg_info_directory, 0755) == -1)
    {
        perror("mkdir");
        return;
    }
    if (mkdir(repo_directory, 0755) == -1)
    {
        perror("mkdir");
        return;
    }
    if (mkdir(bootstrap_directory, 0755) == -1)
    {
        perror("mkdir");
        return;
    }
}
