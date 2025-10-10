/*
 * src/lock.c
 *
 * Copyright (c) 2024 Omar Berrow
 */

#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "lock.h"

char* current_lock_name = "/obos-strap-lock";

sem_t* current_lock;

void unlock_sig(int signum)
{
    const char* signame = "unknown";
    switch (signum)
    {
        case SIGFPE:
            signame = "SIGFPE";
            break;
        case SIGINT:
            signame = "SIGINT";
            break;
        case SIGSEGV:
            signame = "SIGSEGV";
            break;
        case SIGILL:
            signame = "SIGILL";
            break;
    }
    fprintf(stderr, "FATAL: Recieved %s\nAborting. Repository might be in an invalid state.\n", signame);
    unlock();
    if (signum != SIGINT)
        abort();
    else
        exit(1);
}

void lock()
{
    // printf("%s: Acquiring lock\n", g_argv[0]);
#if !OBOS_STRAP_MLIBC
    current_lock = sem_open(current_lock_name, O_CREAT, 0666, 1);
    if (!current_lock)
    {
        perror("sem_open");
        exit(errno);
    }
    sem_wait(current_lock);
    atexit(unlock);
#endif
    signal(SIGINT, unlock_sig);
    signal(SIGSEGV, unlock_sig);
    signal(SIGFPE, unlock_sig);
    signal(SIGILL, unlock_sig);
}

void unlock()
{
#if OBOS_STRAP_MLIBC
    return;
#endif
    if (!current_lock) return;
    sem_post(current_lock);
    sem_close(current_lock);
    sem_unlink(current_lock_name);
    current_lock = NULL;
}
void unlock_forced()
{
    current_lock = sem_open(current_lock_name, O_CREAT, 0666, 1);
    if (!current_lock)
    {
        perror("sem_open");
        exit(errno);
    }
    sem_post(current_lock);
    sem_close(current_lock);
    sem_unlink(current_lock_name);
    current_lock = NULL;
}
