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

#if _XOPEN_SOURCE >= 500
static int remove_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    if ((strcmp(fpath, "README") == 0) || (strcmp(fpath, pkg_info_directory) == 0))
        return 0;
    switch (typeflag) {
        case FTW_D:
            rmdir(fpath);
            break;
        default:
            remove(fpath);
            break;
    }
    return 0;
}

void remove_recursively(const char* path)
{
    nftw(path, remove_cb, 64, FTW_DEPTH | FTW_PHYS);
}
#else
static int remove_cb(const char *fpath, const struct stat *sb, int typeflag)
{
    switch (typeflag) {
        case FTW_D:
            rmdir(fpath);
            break;
        default:
            remove(fpath);
            break;
    }
    return 0;
}

static void remove_recursively(const char* path)
{
    ftw(path, remove_cb, 64);
}
#endif

void clean()
{
    remove_recursively(pkg_info_directory);
}
