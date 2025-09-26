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
int command_array_run(command_array* arr, command** cmd);

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

union package_version {
    struct {
        uint8_t major;
        uint8_t minor;
        uint8_t patch;
    };
    uint8_t arr[3];
    uint32_t integer;
} __attribute__((packed));
static inline bool version_less_than(const union package_version lhs, const union package_version rhs)
{
    if (lhs.major < rhs.major) return true;
    if (lhs.minor < rhs.minor) return true;
    if (lhs.patch < rhs.patch) return true;
    return false;
}

enum {
    VERSION_CMP_NONE,
    VERSION_CMP_LESS,   
    VERSION_CMP_LESS_EQUAL,   
    VERSION_CMP_GREATER,   
    VERSION_CMP_GREATER_EQUAL,   
    VERSION_CMP_EQUAL,
};
bool do_version_cmp(int how, union package_version lhs, union package_version rhs);
// If *how_cmp is set to VERSION_CMP_NONE, then *depend = depend_expr
void parse_depend_expr(const char* depend_expr, char** depend, union package_version* depend_version, int* how_cmp);

enum {
    BUILD_STATE_CLEAN = 0,
    BUILD_STATE_FETCHED,
    BUILD_STATE_CONFIGURED,
    BUILD_STATE_BUILT,
    BUILD_STATE_INSTALLED,
};
struct pkginfo {
    uint8_t build_state;
    union {
        struct {
            struct timeval configure_date;
            struct timeval build_date;
            struct timeval install_date;
        };
        struct timeval build_times[3];
    };
    uint64_t cross_compiled;
    union package_version version;
    __attribute__((aligned(1))) uint8_t resv[32 - sizeof (union package_version)];
    uint64_t host_triplet_len;
    char host_triplet[]; // the triplet of the host this package is intended to run on.
};

// NOTE: Creates a new struct pkginfo and writes it to disk, if it is unpresent.
struct pkginfo* read_package_info(const char* pkg_name);
void write_package_info(const char* pkg_name, struct pkginfo* info);

typedef struct package {
    const char* config_file_path;

    const char *name;
    const char* description;

    const char* host_provides;

    char* bin_package_prefix;

    struct timeval recipe_mod_time;

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

    union package_version version;

    bool host_package : 1;
    bool supports_binary_packages : 1;
    bool inhibit_auto_rebuild : 1;
} package;
char* package_make_bin_prefix(package* pkg);
// info can be NULL
bool package_outdated(package* pkg, struct pkginfo* info, int since_state);

package* get_package(const char* pkg_name);

int run_command(const char* proc, string_array argv);
int run_command_supress_output(const char* proc, string_array argv);
