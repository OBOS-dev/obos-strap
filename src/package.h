/*
 * src/package.h
 *
 * Copyright (c) 2024 Omar Berrow
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct string_array {
    char** buf;
    size_t cnt;
} string_array;

void string_array_append(string_array* arr, const char* str);
const char* string_array_at(string_array* arr, size_t idx);
void string_array_free(string_array* arr);

typedef struct command {
    const char* proc;
    string_array argv;
} command;

typedef struct command_array {
    command* buf;
    size_t cnt;
} command_array;

void command_array_append(command_array* arr, command* cmd);
command* command_array_at(command_array* arr, size_t idx);
void command_array_free(command_array* arr);

typedef struct package {
    const char* config_file_path;
    const char *name;
    const char* description;
    union {
        struct {
            const char* git_commit;
            const char* git_url;
        } git;
        struct {
            const char* url;
        } web;
    } download;
    string_array depends;
    string_array patches;
    command_array build_commands;
    command_array install_commands;
    command_array bootstrap_commands;
} package;

package* get_package(const char* pkg_name);

int run_command(const char* proc, string_array argv);