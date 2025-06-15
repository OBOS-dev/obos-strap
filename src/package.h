/*
 * src/package.h
 *
 * Copyright (c) 2024 Omar Berrow
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <sys/time.h>
#include <stdbool.h>

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

typedef struct patch {
    const char* patch;
    const char* modifies;
    bool delete_file;
} patch;
typedef struct patch_array {
    patch* buf;
    size_t cnt;
} patch_array;
void patch_array_append(patch_array* arr, patch* ptch);
patch* patch_array_at(patch_array* arr, size_t idx);
void patch_array_free(patch_array* arr);

typedef struct package {
    const char* config_file_path;

    const char *name;
    const char* description;

    const char* host_provides;

    union {
        struct {
            const char* git_commit;
            const char* git_url;
        } git;
        struct {
            const char* url;
        } web;
    } source;
    enum {
        SOURCE_TYPE_SOURCELESS,
        SOURCE_TYPE_GIT,
        SOURCE_TYPE_WEB,
    } source_type;
    patch_array patches;

    string_array depends;

    command_array build_commands;
    command_array install_commands;
    command_array bootstrap_commands;
    command_array run_commands;

    bool host_package;
} package;

package* get_package(const char* pkg_name);

int run_command(const char* proc, string_array argv);

enum {
    BUILD_STATE_CLEAN = 0,
    BUILD_STATE_FETCHED,
    BUILD_STATE_CONFIGURED,
    BUILD_STATE_BUILT,
    BUILD_STATE_INSTALLED,
};
struct pkginfo {
    uint8_t build_state;
    struct timeval configure_date;
    struct timeval build_date;
    struct timeval install_date;
    uint64_t cross_compiled;
    uint64_t resv[4];
    uint64_t host_triplet_len;
    char host_triplet[]; // the triplet of the host this package is intended to run on.
};

// NOTE: Creates a new struct pkginfo and writes it to disk, if it is unpresent.
struct pkginfo* read_package_info(const char* pkg_name);
void write_package_info(const char* pkg_name, struct pkginfo* info);
