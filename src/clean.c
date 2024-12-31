/*
 * src/clean.c
 *
 * Copyright (c) 2024 Omar Berrow
 */

#include <stdint.h>
#include <stdio.h>

#include "path.h"

void clean()
{
    remove(prefix_directory);
    remove(repo_directory);
    remove(bootstrap_directory);
}
