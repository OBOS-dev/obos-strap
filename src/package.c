/*
 * src/package.c
 *
 * Copyright (c) 2024 Omar Berrow
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <errno.h>
#include <ctype.h>

#include "package.h"
#include "path.h"

#include <cjson/cJSON.h>

void string_array_append(string_array* arr, const char* str)
{
    arr->buf = realloc(arr->buf,  (arr->cnt+1)*sizeof(command));
    if (!str)
    {
        arr->buf[arr->cnt++] = NULL;
        return;
    }
    size_t str_len = strlen(str);
    arr->buf[arr->cnt++] = memcpy(malloc(str_len+1), str, str_len);
    arr->buf[arr->cnt - 1][str_len] = 0;
}
const char* string_array_at(string_array* arr, size_t idx)
{
    if (idx >= arr->cnt)
        return NULL;
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
    arr->buf = realloc(arr->buf, (arr->cnt+1)*sizeof(command));
    memcpy(&arr->buf[arr->cnt++], cmd, sizeof(*cmd));
}

command* command_array_at(command_array* arr, size_t idx)
{
    if (idx >= arr->cnt)
        return NULL;
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
    return cJSON_GetStringValue(child);
}

static int get_str_array_field(cJSON* parent, const char* fieldname, string_array* arr)
{
    cJSON* child = cJSON_GetObjectItem(parent, fieldname);
    if (!child)
        return -1;
    cJSON *i = NULL;
    cJSON_ArrayForEach(i, child)
    {
        if (!cJSON_IsString(i))
        {
            printf("%s: Invalid array value in package JSON in field '%s', ignoring.\n", g_argv[0], fieldname);
            continue;
        }
        string_array_append(arr, cJSON_GetStringValue(i));
    }
    return 0;
}

static int get_command_array(cJSON* parent, const char* fieldname, command_array* arr)
{
    cJSON* child = cJSON_GetObjectItem(parent, fieldname);
    if (!child)
        return -1;
    cJSON *i = NULL;
    cJSON_ArrayForEach(i, child)
    {
        if (!cJSON_IsArray(i))
        {
            printf("%s: Invalid array value in package JSON in field '%s', ignoring.\n", g_argv[0], fieldname);
            continue;
        }
        command cmd = {};
        cJSON *j = NULL;
        cJSON_ArrayForEach(j, i)
        {
            if (!cJSON_IsString(j))
            {
                printf("%s: Invalid array value (expected string) in package JSON in field '%s', ignoring.\n", g_argv[0], fieldname);
                continue;
            }
            string_array_append(&cmd.argv, cJSON_GetStringValue(j));
        }
        if (!cmd.argv.cnt)
        {
            printf("%s: No command defined in package JSON in field '%s', ignoring command.\n", g_argv[0], fieldname);
            continue;
        }
        cmd.proc = string_array_at(&cmd.argv, 0);
        command_array_append(arr, &cmd);
    }
    return 0;
}

static int parse_dollar_sign(char* dollar_sign, const char* fieldname, char** const arg_, size_t* const arglen_, size_t *nSubstituted, package* pkg)
{
    char* arg = *arg_;
    size_t arglen = *arglen_;
    const char* subst_str = NULL;
    size_t subst_len = 0;
    bool subst_free = false;

    const char *act = dollar_sign + 1;
    // NOTE: This is the length from dollar_sign to the end of the key
    size_t act_len = 0;
    switch (*act)
    {
        case '$':
        {
            // Escape the dollar sign.
            subst_str = "$";
            subst_len = 1;
            subst_free = false;
            act_len = 2;
            break;
        }
        case '{':
        {
            subst_str = act;
            if (subst_str >= (arg+arglen))
            {
                *nSubstituted = 1;
                return 0;
            }
            const char* iter = subst_str;
            while (*iter != '}' && *iter)
                iter++;
            if (*iter != '}')
            {
                printf("%s: In field '%s': Syntax error trying to substitute %s", g_argv[0], fieldname, dollar_sign);
                return -1;
            }
            subst_len = iter-subst_str-1;
            subst_free = false;
            act_len = subst_len+3;
            subst_str++;

            // Parse
            if (strncmp(subst_str, "bootstrap_directory", subst_len) == 0)
            {
                subst_str = bootstrap_directory;
                subst_len = strlen(subst_str);
            }
            else if (strncmp(subst_str, "name", subst_len) == 0)
            {
                subst_str = pkg->name;
                subst_len = strlen(pkg->name);
            }
            else if (strncmp(subst_str, "repo_directory", subst_len) == 0)
            {
                subst_str = repo_directory;
                subst_len = strlen(subst_str);
            }
            else if (strncmp(subst_str, "prefix", subst_len) == 0)
            {
                subst_str = prefix_directory;
                subst_len = strlen(subst_str);
            }
            else if (strncmp(subst_str, "nproc", subst_len) == 0)
            {
                subst_free = true;
                int nproc = get_nprocs();
                subst_len = snprintf(NULL, 0, "%d", nproc);
                subst_str = malloc(subst_len+1);
                snprintf((char*)subst_str, subst_len+1, "%d", nproc);
            }
            else
            {
                printf("%s: In field '%s': Invalid substitution key '%s', aborting.\n", g_argv[0], fieldname, dollar_sign);
                return -1;
            }


            break;
        }
        default:
        {
            // Environment variable substitution.
            subst_str = act;
            if (subst_str >= (arg+arglen))
            {
                *nSubstituted = 1;
                return 0;
            }
            const char* iter = subst_str;
            while (!isspace(*iter) && *iter)
                iter++;
            subst_len = iter-subst_str;
            subst_free = false;
            act_len = subst_len+1;

            // Fetch the enviornment variable.
            char* env = malloc(subst_len+1);
            memcpy(env, subst_str, subst_len);
            env[subst_len] = 0;
            subst_str = getenv(env);
            free(env);
            if (!subst_str)
            {
                printf("%s: In field '%s': Invalid environment variable '%s', aborting.\n", g_argv[0], fieldname, dollar_sign);
                return -1;
            }
            subst_len = strlen(subst_str);
            break;
        }
    }
    // Replace the entire substitution.
    *nSubstituted = act_len;
    printf("DEBUG: Substituition of %.*s with %.*s\n", (int)act_len, dollar_sign, (int)subst_len, subst_str);
    size_t new_len = arglen - act_len + subst_len;
    char* newarg = malloc(new_len+1);
    size_t front = dollar_sign - arg;
    memcpy(newarg, arg, front);
    memcpy(newarg + front, subst_str, subst_len);
    memcpy(newarg + front + subst_len, arg+front+act_len, new_len-(front+subst_len));
    newarg[new_len] = 0;
    *nSubstituted = subst_len;
    *arg_ = newarg;
    *arglen_ = new_len;
    if (subst_free)
        free((char*)subst_str);
    return 0;
}

// Finds any substitutions that we need to make before executing the command (e.g., ${bootstrap_directory})
static int parse_command_array(const char* fieldname, package* pkg, command_array* arr)
{
    for (size_t i = 0; i < arr->cnt; i++)
    {
        for (size_t j = 0; j < arr->buf[i].argv.cnt; j++)
        {
            char* arg = arr->buf[i].argv.buf[j];
            size_t arglen = strlen(arg);

            char* iter = arg;
            do {
                char* dollar_sign = strchr(iter, '$');
                if (!dollar_sign)
                    break;
                if ((dollar_sign + 1) >= (arg+arglen))
                    break;

                size_t nSubstituted = 0;
                int ec = parse_dollar_sign(dollar_sign, fieldname, &arg, &arglen, &nSubstituted, pkg);
                if (ec != 0)
                    return ec;
                iter = dollar_sign + nSubstituted;
            } while(1);
            arr->buf[i].argv.buf[j] = arg;
            if (j == 0)
                arr->buf[i].proc = arg;
        }
    }

    return 0;
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
    pkg->name = get_str_field(context, "name");
    if (!pkg->name)
    {
        printf("%s: Invalid format or missing field 'name' in package JSON.\n", g_argv[0]);
        free(json_data);
        return NULL;
    }

    if (get_str_array_field(context, "depends", &pkg->depends) != 0)
    {
        printf("%s: Invalid format or missing field 'depends' in package JSON.\n", g_argv[0]);
        free(json_data);
        return NULL;
    }

    if (cJSON_HasObjectItem(context, "git-url"))
    {
        pkg->source.git.git_url = get_str_field(context, "git-url");
        pkg->source.git.git_commit  = get_str_field(context, "git-commit");
        if (!pkg->source.git.git_url)
        {
            printf("%s: Invalid field 'git-url' in json package.\n", g_argv[0]);
            free(json_data);
            free(pkg);
            cJSON_free(context);
            return NULL;
        }
        if (!pkg->source.git.git_commit)
        {
            printf("%s: Invalid format or missing field 'git-commit' in package JSON.\n", g_argv[0]);
            free(json_data);
            free(pkg);
            cJSON_free(context);
            return NULL;
        }
        get_str_array_field(context, "patches", &pkg->patches);
        pkg->source_type = SOURCE_TYPE_GIT;
    }
    else if (cJSON_HasObjectItem(context, "url"))
    {
        pkg->source.web.url = get_str_field(context, "url");
        pkg->source_type = SOURCE_TYPE_WEB;
    }
    else
    {
        printf("%s: Could not determine package source for package '%s'\n", g_argv[0], pkg->name);
        free(json_data);
        free(pkg);
        cJSON_free(context);
        return NULL;
    }

    if (get_command_array(context, "bootstrap-commands", &pkg->bootstrap_commands) != 0)
    {
        printf("%s: Invalid format or missing field 'bootstrap-commands' in package JSON.\n", g_argv[0]);
        free(json_data);
        free(pkg);
        cJSON_free(context);
        return NULL;
    }
    if (get_command_array(context, "build-commands", &pkg->build_commands) != 0)
    {
        printf("%s: Invalid format or missing field 'bootstrap-commands' in package JSON.\n", g_argv[0]);
        free(json_data);
        free(pkg);
        cJSON_free(context);
        return NULL;
    }
    if (get_command_array(context, "install-commands", &pkg->install_commands) != 0)
    {
        printf("%s: Invalid format or missing field 'bootstrap-commands' in package JSON.\n", g_argv[0]);
        free(json_data);
        free(pkg);
        cJSON_free(context);
        return NULL;
    }

    if (parse_command_array("bootstrap-commands", pkg, &pkg->bootstrap_commands) != 0)
    {
        free(json_data);
        free(pkg);
        cJSON_free(context);
        return NULL;
    }
    if (parse_command_array("build-commands", pkg, &pkg->build_commands) != 0)
    {
        free(json_data);
        free(pkg);
        cJSON_free(context);
        return NULL;
    }
    if (parse_command_array("install-commands", pkg, &pkg->install_commands) != 0)
    {
        free(json_data);
        free(pkg);
        cJSON_free(context);
        return NULL;
    }

    pkg->description = get_str_field(context, "description");

    return pkg;
}

#define pkg_info_format "%s/pkginfo_%s.bin"
struct pkginfo* read_package_info(const char* pkg_name)
{
    size_t pathlen = snprintf(NULL, 0, pkg_info_format, pkg_info_directory, pkg_name);
    char* path = malloc(pathlen+1);
    snprintf(path, pathlen+1, pkg_info_format, pkg_info_directory, pkg_name);
    FILE* file = fopen(path, "r+");
    if (!file)
    {
        if (errno != ENOENT)
            perror("fopen");
        else {
            struct pkginfo* info = calloc(1, sizeof(struct pkginfo));
            info->build_state = BUILD_STATE_CLEAN;
            file = fopen(path, "w");
            if (file)
            {
                fwrite(info, sizeof(*info), 1, file);
                fclose(file);
            }
            free(path);
            return info;
        }
        return NULL;
    }
    free(path);
    struct pkginfo* info = malloc(sizeof(struct pkginfo));
    fread(info, sizeof(*info), 1, file);
    fclose(file);

    if (info->build_state > BUILD_STATE_INSTALLED)
    {
        fprintf(stderr, "%s: Invalid or corrupt package info for package %s\n", g_argv[0], pkg_name);
        exit(-1);
    }

    struct timeval current_time = {};
    gettimeofday(&current_time, NULL);

    if (info->configure_date.tv_sec > current_time.tv_sec)
    {
        fprintf(stderr, "%s: Invalid or corrupt package info for package %s\n", g_argv[0], pkg_name);
        exit(-1);
    }
    if (info->build_date.tv_sec > current_time.tv_sec)
    {
        fprintf(stderr, "%s: Invalid or corrupt package info for package %s\n", g_argv[0], pkg_name);
        exit(-1);
    }
    if (info->install_date.tv_sec > current_time.tv_sec)
    {
        fprintf(stderr, "%s: Invalid or corrupt package info for package %s\n", g_argv[0], pkg_name);
        exit(-1);
    }

    return info;
}

void write_package_info(const char* pkg_name, struct pkginfo* info)
{
    size_t pathlen = snprintf(NULL, 0, pkg_info_format, pkg_info_directory, pkg_name);
    char* path = malloc(pathlen+1);
    snprintf(path, pathlen+1, pkg_info_format, pkg_info_directory, pkg_name);
    FILE* file = fopen(path, "r+");
    free(path);
    if (!file)
    {
        perror("fopen");
        return;
    }
    fwrite(info, sizeof(*info), 1, file);
    fclose(file);
}
