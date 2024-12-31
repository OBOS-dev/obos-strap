/*
 * src/package.c
 *
 * Copyright (c) 2024 Omar Berrow
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include "package.h"
#include "path.h"

#include <cjson/cJSON.h>

void string_array_append(string_array* arr, const char* str)
{
    arr->buf = realloc(arr->buf, arr->cnt+1);
    size_t str_len = strlen(str);
    arr->buf[arr->cnt++] = memcpy(malloc(str_len+1), str, str_len);
    arr->buf[arr->cnt - 1][str_len] = 0;
}
const char* string_array_at(string_array* arr, size_t idx)
{
    return arr->buf[idx];
}
void string_array_free(string_array* arr)
{
    for (size_t i = 0; i < arr->cnt; i++)
        free(arr->buf[i]);
    free(arr->buf);
}

void command_array_append(command_array* arr, command* cmd)
{
    arr->buf = realloc(arr->buf, arr->cnt+1);
    memcpy(&arr->buf[arr->cnt++], cmd, sizeof(*cmd));
}

command* command_array_at(command_array* arr, size_t idx)
{
    return &arr->buf[idx];
}

void command_array_free(command_array* arr)
{
    for (size_t i = 0; i < arr->cnt; i++)
        string_array_free(&arr->buf[i].argv);
    free(arr->buf);
}

static const char* get_str_field(cJSON* parent, const char* fieldname)
{
    cJSON* child = cJSON_GetObjectItem(parent, fieldname);
    return cJSON_GetStringValue(parent);
}

package* get_package(const char* pkg_name)
{
    if (strlen(pkg_name) == 0)
        return NULL;

    char* pkg_path = NULL;
    size_t sz_path = snprintf(pkg_path, 0, "%s/%s.json", recipes_directory, pkg_name);
    pkg_path = malloc(sz_path + 1);
    pkg_path[sz_path] = 0;
    snprintf(pkg_path, sz_path+1, "%s/%s.json", recipes_directory, pkg_name);

    // FIXME: TOCTOU?

    struct stat st = {};
    if (stat(pkg_path, &st) == -1)
        return NULL;

    FILE* pkg_json = fopen(pkg_path, "r");
    if (!pkg_json)
        return NULL;

    char* json_data = malloc(st.st_size);
    fread(json_data, st.st_size, st.st_size, pkg_json);

    fclose(pkg_json);

    cJSON* context = cJSON_Parse(json_data);
    if (!context)
    {
        printf("%s: Parsing package JSON failed.\n", g_argv[0]);
        free(json_data);
        return NULL;
    }

    package* pkg = calloc(1, sizeof(package));

    // Populate the package info.
    pkg->config_file_path = pkg_path;
    pkg->description = get_str_field(context, "description");

    return pkg;
}
