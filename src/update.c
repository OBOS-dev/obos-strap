/*
 * src/update.c
 *
 * Copyright (c) 2025 Omar Berrow
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "package.h"

#include <sys/stat.h>

static const char update_script[] =
"#!/bin/bash\n\
OLD_HEAD=`git log --pretty=format:'%H' -n 1`\n\
git pull\n\
UPDATED=`git diff --name-only $OLD_HEAD`\n\
IFS=$'\n\n'\n\
for line in $UPDATED; do\n\
    if [[ \"$line\" == \"recipes/\"* ]]; then\n\
        PACKAGE=$line\n\
        PACKAGE=${PACKAGE%%.json}\n\
        PACKAGE=${PACKAGE#recipes/}\n\
        yes | ./obos-strap rebuild $PACKAGE\n\
    fi\n\
done\n\
unset IFS";

void update()
{
    printf("Updating packages\n");
    char temp[] = { "./obos-strap-XXXXXX" };
    int file = mkstemp(temp);
    write(file, update_script, sizeof(update_script));
    struct stat st = {};
    stat(temp, &st);
    chmod(temp, st.st_mode | S_IXUSR);
    string_array argv = {};
    string_array_append(&argv, temp);
    close(file);
    run_command(temp, argv);
    string_array_free(&argv);
    remove(temp);
}
