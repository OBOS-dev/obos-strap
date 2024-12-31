/*
 * src/build_pkg.c
 *
 * Copyright (c) 2024 Omar Berrow
 */

#include <stdint.h>
#include <stdio.h>

#include "package.h"
#include "path.h"
#include "lock.h"

void build_pkg(const char* name)
{
    lock();
    package* pkg = get_package(name);
    if (!pkg)
    {
        printf("%s: Invalid or unknown package '%s'\nAbort.\n", g_argv[0], name);
        return;
    }
    printf("%s: Building %s...\n", g_argv[0], name);

    unlock();
}
