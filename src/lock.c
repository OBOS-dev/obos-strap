/*
 * src/lock.c
 *
 * Copyright (c) 2024 Omar Berrow
 */

#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "lock.h"

char* current_lock_name = "/obos-strap-lock";

sem_t* current_lock;

void lock()
{
    // printf("%s: Acquiring lock\n", g_argv[0]);
    current_lock = sem_open(current_lock_name, O_CREAT, 0666, 1);
    if (!current_lock)
    {
        perror("sem_open");
        exit(errno);
    }
    sem_wait(current_lock);
    atexit(unlock);
}

void unlock()
{
    if (!current_lock) return;
    sem_post(current_lock);
    sem_close(current_lock);
    sem_unlink(current_lock_name);
    current_lock = NULL;
}
