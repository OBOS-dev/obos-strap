/*
 * src/cmd.c
 *
 * Copyright (c) 2024 Omar Berrow
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "package.h"

int run_command(const char* proc, string_array argv)
{
#if __unix__
    pid_t pid = fork();
    if (pid == -1)
    {
        perror("fork");
        return -1;
    }
    else if (pid == 0)
    {
        execv(proc, argv.buf);
        exit(EXIT_FAILURE);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) == -1)
    {
        perror("waitpid");
        exit(EXIT_FAILURE);
    }

    return WEXITSTATUS(status);

#else
#   error run_command needs an implementation for your target.
#endif
}
