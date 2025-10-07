/*
 * src/package.c
 *
 * Copyright (c) 2024-2025 Omar Berrow
 */

#define _DEFAULT_SOURCE

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
#include <errno.h>
#include <ctype.h>
#include <assert.h>
#include <dirent.h>

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

int command_array_run(command_array* arr, command** ocmd)
{
    if (!arr->cnt)
        return 0;
    // for (size_t i = 0; i < arr->cnt; i++)
    // {
    //     command* cmd = &arr->buf[i];
    //     if (ocmd) *ocmd = cmd;
    //     int ec = run_command(cmd->proc, cmd->argv);
    //     if (ec != EXIT_SUCCESS)
    //         return ec;
    // }
    char* cmds_str = NULL;
    size_t cmds_str_size = 0;
    for (size_t i = 0; i < arr->cnt; i++)
    {
        char* cmd_str = NULL;
        size_t cmd_str_size = 0;
        command* cmd = &arr->buf[i];
        for (size_t arg = 0; arg < cmd->argv.cnt; arg++)
            cmd_str_size += strlen(cmd->argv.buf[arg]) + 3;
        cmd_str = malloc(cmd_str_size + 1);
        cmd_str[0] = 0;
        cmd_str[cmd_str_size] = 0;
        for (size_t arg = 0; arg < cmd->argv.cnt; arg++)
        {
            bool has_asterisk = strchr(cmd->argv.buf[arg], '*') != NULL;
            if (!has_asterisk)
                strcat(cmd_str, "\"");
            strcat(cmd_str, cmd->argv.buf[arg]); // just hope this works ;)
            if (!has_asterisk)
            {
                if (arg != cmd->argv.cnt-1)
                    strcat(cmd_str, "\" ");
                else
                    strcat(cmd_str, "\"");
            }
            else if (arg != cmd->argv.cnt-1)
                strcat(cmd_str, " ");
        }
        cmds_str_size += cmd_str_size+1;
        cmds_str = realloc(cmds_str, cmds_str_size+1);
        cmds_str[cmds_str_size] = 0;
        if (cmds_str_size == (cmd_str_size+1))
            cmds_str[0] = 0;
        strcat(cmds_str, cmd_str);
        strcat(cmds_str, ";");
        free(cmd_str);
    }
    string_array argv = {};
    string_array_append(&argv, "bash");
    string_array_append(&argv, "-c");
    string_array_append(&argv, cmds_str);
    int ec = run_command("bash", argv);
    free(cmds_str);
    if (ocmd)
        *ocmd = &arr->buf[0];
    return ec;
}

void patch_array_append(patch_array* arr, patch* ptch)
{
    arr->buf = realloc(arr->buf, (arr->cnt+1)*sizeof(patch));
    memcpy(&arr->buf[arr->cnt++], ptch, sizeof(*ptch));
}

patch* patch_array_at(patch_array* arr, size_t idx)
{
    if (idx >= arr->cnt)
        return NULL;
    return &arr->buf[idx];
}

void patch_array_free(patch_array* arr)
{
    for (size_t i = 0; i < arr->cnt; i++)
        free(&arr->buf[i]);
    free(arr->buf);
}

static int parse_dollar_sign(char* dollar_sign, const char* fieldname, char** const arg_, size_t* const arglen_, size_t *nSubstituted, package* pkg);

static const char* get_str_field(cJSON* parent, const char* fieldname)
{
    cJSON* child = cJSON_GetObjectItem(parent, fieldname);
    return cJSON_GetStringValue(child);
}
static const char* get_str_field_subst(cJSON* parent, const char* fieldname, package* pkg)
{
    const char* field = get_str_field(parent, fieldname);
    if (!field)
        return NULL;
    size_t len = strlen(field);
    char* arg = memcpy(malloc(len+1), field, len);
    arg[len] = 0;
    char* iter = arg;
    do {
        char* dollar_sign = strchr(iter, '$');
        if (!dollar_sign)
            break;
        if ((dollar_sign + 1) >= (arg+len))
            break;
        size_t dollar_sign_offset = dollar_sign-arg;

        size_t nSubstituted = 0;
        int ec = parse_dollar_sign(dollar_sign, fieldname, &arg, &len, &nSubstituted, pkg);
        if (ec != 0)
            return NULL;
        iter = arg + dollar_sign_offset;
        dollar_sign = iter;
        iter = dollar_sign + nSubstituted;
    } while(1);

    return arg;
}

static bool get_boolean_field(cJSON* parent, const char* fieldname, bool default_ret)
{
    cJSON *obj = cJSON_GetObjectItem(parent, fieldname);
    if (!obj)
        return default_ret;
    return cJSON_IsTrue(obj);
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
//          printf("%s: No command defined in package JSON in field '%s', ignoring command.\n", g_argv[0], fieldname);
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
            else if (strncmp(subst_str, "description", subst_len) == 0)
            {
                subst_str = pkg->description;
                subst_len = strlen(pkg->description);
            }
            else if (strncmp(subst_str, "repo_directory", subst_len) == 0)
            {
                subst_str = repo_directory;
                subst_len = strlen(subst_str);
            }
            else if (strncmp(subst_str, "prefix", subst_len) == 0)
            {
                subst_str = pkg->host_package ? host_prefix_directory : prefix_directory;
                subst_len = strlen(subst_str);
            }
            else if (strncmp(subst_str, "target_prefix", subst_len) == 0)
            {
                subst_str = prefix_directory;
                subst_len = strlen(subst_str);
            }
            else if (strncmp(subst_str, "host_prefix", subst_len) == 0)
            {
                subst_str = host_prefix_directory;
                subst_len = strlen(subst_str);
            }
            else if (strncmp(subst_str, "root_directory", subst_len) == 0)
            {
                subst_str = root_directory;
                subst_len = strlen(subst_str);
            }
            else if (strncmp(subst_str, "nproc", subst_len) == 0)
            {
                subst_free = true;
                int nproc = sysconf(_SC_NPROCESSORS_ONLN);
                subst_len = snprintf(NULL, 0, "%d", nproc);
                subst_str = malloc(subst_len+1);
                snprintf((char*)subst_str, subst_len+1, "%d", nproc);
            }
            else if (strncmp(subst_str, "target_triplet", subst_len) == 0)
            {
                subst_str = g_config.target_triplet;
                subst_len = strlen(subst_str);
            }
            else if (strncmp(subst_str, "host_triplet", subst_len) == 0)
            {
                subst_str = g_config.host_triplet;
                subst_len = strlen(subst_str);
            }
            else if (strncmp(subst_str, "version", subst_len) == 0)
            {
                subst_free = true;
                subst_len = snprintf(NULL, 0, "%u.%u.%u", pkg->version.major, pkg->version.minor, pkg->version.patch);
                subst_str = malloc(subst_len+1);
                snprintf((char*)subst_str, subst_len+1, "%u.%u.%u", pkg->version.major, pkg->version.minor, pkg->version.patch);
                ((char*)subst_str)[subst_len] = 0;
            }
            else if (strncmp(subst_str, "bin_package_prefix", subst_len) == 0)
            {
                subst_str = package_make_bin_prefix(pkg);
                subst_len = strlen(pkg->bin_package_prefix);
                pkg->supports_binary_packages = true;
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
#ifndef NDEBUG
    printf("DEBUG: Substituition of %.*s with %.*s\n", (int)act_len, dollar_sign, (int)subst_len, subst_str);
#endif
    size_t new_len = arglen - act_len + subst_len;
    char* newarg = malloc(new_len+1);
    size_t front = dollar_sign - arg;
    memcpy(newarg, arg, front);
    memcpy(newarg + front, subst_str, subst_len);
    memcpy(newarg + front + subst_len, arg+front+act_len, new_len-(front+subst_len));
    newarg[new_len] = 0;
    *nSubstituted = subst_len + (*act == '$');
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
                size_t dollar_sign_offset = dollar_sign-arg;

                size_t nSubstituted = 0;
                int ec = parse_dollar_sign(dollar_sign, fieldname, &arg, &arglen, &nSubstituted, pkg);
                if (ec != 0)
                    return ec;
                iter = arg + dollar_sign_offset;
                dollar_sign = iter;
                iter = dollar_sign + nSubstituted;
            } while(1);
            arr->buf[i].argv.buf[j] = arg;
            if (j == 0)
                arr->buf[i].proc = arg;
        }
    }

    return 0;
}

int get_patch_array(cJSON* parent, const char* fieldname, patch_array* arr)
{
    cJSON* child = cJSON_GetObjectItem(parent, fieldname);
    if (!child)
        return -1;
    cJSON *i = NULL;
    cJSON_ArrayForEach(i, child)
    {
        if (!cJSON_IsObject(i))
        {
            printf("%s: Invalid array value in package JSON in field '%s', ignoring.\n", g_argv[0], fieldname);
            continue;
        }
        patch ptch = {};
        ptch.patch = get_str_field(i, "patch");
        if (!ptch.patch)
        {
            printf("%s: Invalid array value in package JSON in field '%s', ignoring.\n", g_argv[0], fieldname);
            continue;
        }
        ptch.modifies = get_str_field(i, "modifies");
        if (!ptch.modifies)
        {
            printf("%s: Invalid array value in package JSON in field '%s', ignoring.\n", g_argv[0], fieldname);
            continue;
        }
        do {
            cJSON* child2 = cJSON_GetObjectItem(i, "delete-file");
            ptch.delete_file = !child2 ? false : !!cJSON_GetNumberValue(child2);
        } while (0);
        patch_array_append(arr, &ptch);
    }
    return 0;
}

char* package_make_bin_prefix(package* pkg)
{
    if (pkg->bin_package_prefix)
        return pkg->bin_package_prefix;
    char* buf = NULL;
    size_t len = 0;

    len = snprintf(NULL, 0, "%s/%s/obos-strap-bin/", bootstrap_directory, pkg->name);
    buf = malloc(len+1);
    snprintf(buf, len+1, "%s/%s/obos-strap-bin/", bootstrap_directory, pkg->name);
    pkg->bin_package_prefix = buf;

    return buf;
}

bool package_outdated(package* pkg, struct pkginfo* info, int since_state)
{
    if (!pkg)
        return false; // a package that doesn't exist isn't outdated (or is it?)
    if (!info)
        info = read_package_info(pkg->name);
    assert(info);

    // If we change package host triplets
    if (strncmp(pkg->host_package ? g_config.host_triplet : g_config.target_triplet, info->host_triplet, info->host_triplet_len) != 0)
        return true;
   
    if (info->build_state > since_state)
        return true;
    
    if (!do_version_cmp(VERSION_CMP_EQUAL, info->version, pkg->version))
        return true;
    
    struct timeval file_time = pkg->recipe_mod_time;    
    struct timeval cmp_time = info->build_times[info->build_state - BUILD_STATE_CONFIGURED];

    return timercmp(&cmp_time, &file_time, <);
}

static bool get_version(cJSON* context, const char* field_name, union package_version* out)
{
    cJSON* child_ctx = cJSON_GetObjectItem(context, field_name);
    if (!child_ctx)
        return false;
    union package_version ret = {};
    cJSON *elements[3] = {
        cJSON_GetArrayItem(child_ctx, 0),
        cJSON_GetArrayItem(child_ctx, 1),
        cJSON_GetArrayItem(child_ctx, 2),
    };
    for (int i = 0; i < 3; i++)
        if (elements[i] ? !cJSON_IsNumber(elements[i]) : false)
            return false;
    for (int i = 0; i < 3; i++)
        ret.arr[i] = elements[i] ? (int)cJSON_GetNumberValue(elements[i]) : 0;
    *out = ret;
    return true;
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

    pkg->recipe_mod_time.tv_sec = st.st_mtim.tv_sec;
    pkg->recipe_mod_time.tv_usec = st.st_mtim.tv_nsec / 1000;

    pkg->inhibit_auto_rebuild = get_boolean_field(context, "inhibit-auto-rebuild", false);

    // Populate the package info.
    pkg->config_file_path = pkg_path;
    pkg->name = get_str_field(context, "name");
    if (!pkg->name)
    {
        printf("%s: Invalid format or missing field 'name' in package JSON.\n", g_argv[0]);
        free(json_data);
        return NULL;
    }
    if (!get_version(context, "version", &pkg->version))
    {
        printf("%s: Invalid format or missing field 'version' in package JSON.\n", g_argv[0]);
        free(json_data);
        return NULL;
    }

    if (get_str_array_field(context, "depends", &pkg->depends) != 0)
    {
        printf("%s: Invalid format or missing field 'depends' in package JSON.\n", g_argv[0]);
        free(json_data);
        return NULL;
    }

    do {
        cJSON* child = cJSON_GetObjectItem(context, "host-package");
        pkg->host_package = !child ? false : (!!cJSON_GetNumberValue(child) && g_config.cross_compiling);
    } while (0);

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
        pkg->source_type = SOURCE_TYPE_GIT;
    }
    else if (cJSON_HasObjectItem(context, "url"))
    {
        pkg->source.web.url = get_str_field(context, "url");
        pkg->source_type = SOURCE_TYPE_WEB;
    }
    else
        pkg->source_type = SOURCE_TYPE_SOURCELESS;
/*  else
    {
        printf("%s: Could not determine package source for package '%s'\n", g_argv[0], pkg->name);
        free(json_data);
        free(pkg);
        cJSON_free(context);
        return NULL;
    }*/

    get_patch_array(context, "patches", &pkg->patches);
    if (pkg->host_package)
        pkg->host_provides = get_str_field_subst(context, "host-provides", pkg);

    pkg->description = get_str_field(context, "description");

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

    if (get_command_array(context, "run-commands", &pkg->run_commands) == 0)
        parse_command_array("run-commands", pkg, &pkg->run_commands);

    return pkg;
}

#define pkg_info_format "%s/pkginfo_%s.bin"
struct pkginfo* read_package_info(const char* pkg_name)
{
    return read_package_info_ex(pkg_name, true, true);
}

struct pkginfo* read_package_info_ex(const char* pkg_name, bool create, bool validate)
{
    size_t pathlen = snprintf(NULL, 0, pkg_info_format, pkg_info_directory, pkg_name);
    char* path = malloc(pathlen+1);
    snprintf(path, pathlen+1, pkg_info_format, pkg_info_directory, pkg_name);
    FILE* file = fopen(path, "r+");
    if (!file)
    {
        if (errno != ENOENT)
            perror("fopen");
        else if (create) {
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
    struct stat st = {};
    fstat(fileno(file), &st);
    struct pkginfo* info = malloc(st.st_size+1);
    size_t nRead = 0;
    while ((nRead += fread(((char*)info) + nRead, 1, st.st_size - nRead, file)) < st.st_size)
    {
        if (feof(file))
        {
            fprintf(stderr, "Unexpected EOF while reading pkginfo for package '%s'\n", pkg_name);
            abort();
        }
        else if (ferror(file))
        {
            fprintf(stderr, "Unexpected error '%s' while reading pkginfo for package '%s'\n", strerror(errno), pkg_name);
            abort();
        }
    }
    fclose(file);

    if (info->build_state > BUILD_STATE_INSTALLED)
    {
        fprintf(stderr, "%s: Invalid or corrupt package info for package %s\n", g_argv[0], pkg_name);
        exit(-1);
    }

    if (!validate)
        return info;

    if (info->host_triplet_len == 0 && info->build_state >= BUILD_STATE_CONFIGURED)
    {
        fprintf(stderr, "%s: Outdated package info for package %s. Run %s rebuild %s to rebuild the package.\n", g_argv[0], pkg_name, g_argv[0], pkg_name);
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

    if (info->cross_compiled != 0 && info->cross_compiled != 1)
    {
        fprintf(stderr, "%s: Invalid or corrupt package info for package %s\n", g_argv[0], pkg_name);
        exit(-1);
    }

    if (info->cross_compiled != g_config.cross_compiling && info->build_state >= BUILD_STATE_CONFIGURED)
    {
        fprintf(stderr, "%s: Package %s was %scross compiled, but current configuration says we are %scross compiling.\n", 
            g_argv[0], pkg_name,
            info->cross_compiled ? "" : "not ",
            g_config.cross_compiling ? "" : "not ");
        exit(-1);
    }

    if (strncmp(info->host_triplet, g_config.host_triplet, info->host_triplet_len) != 0 &&
        strncmp(info->host_triplet, g_config.target_triplet, info->host_triplet_len) != 0 && 
        info->build_state > BUILD_STATE_CONFIGURED)
    {
        fprintf(stderr, "%s: Package %s triplet mismatch.\n", g_argv[0], pkg_name);
        info->build_state = BUILD_STATE_CLEAN;
    }

    return info;
}

// Format:
// name[operator,version]
// Example:
// bash>=5.3.0
// cross-gcc==14.2.0
// binutils<=2.45
void parse_depend_expr(const char* depend_expr, char** depend, union package_version* depend_version, int* version_cmp)
{
    char* op_location = strrchr(depend_expr, '=');
    if (!op_location)
    {
        char* less_op_location = strrchr(depend_expr, '<');
        char* greater_op_location = strrchr(depend_expr, '>');
        if (less_op_location && greater_op_location)
        {
            *depend = NULL;
            *depend_version = (union package_version){};
            *version_cmp = VERSION_CMP_NONE;
            return;
        }
        op_location = less_op_location ? less_op_location : greater_op_location;
    }
    if (!op_location)
    {
        *depend = (char*)depend_expr;
        *version_cmp = VERSION_CMP_NONE;
        *depend_version = (union package_version){};
        return;
    }
    if (op_location == depend_expr)
    {
        *depend = NULL;
        *depend_version = (union package_version){};
        *version_cmp = VERSION_CMP_NONE;
        return;
    }
    char* op_end = NULL;
    if (*op_location == '=')
    {
        switch (*(op_location - 1)) {
            case '<': *version_cmp = VERSION_CMP_LESS_EQUAL; break;
            case '>': *version_cmp = VERSION_CMP_GREATER_EQUAL; break;
            default: *version_cmp = VERSION_CMP_EQUAL; op_end = op_location + 1; goto out;
        }
        op_end = op_location + 1;
        op_location--;
    }
    else if (*op_location == '<')
    {
        *version_cmp = VERSION_CMP_LESS;
        op_end = op_location + 1;
    }
    else if (*op_location == '>')
    {
        *version_cmp = VERSION_CMP_GREATER;
        op_end = op_location + 1;
    }
    else
        abort(); // impossible
    out:
    (void)0;
    union package_version version = {};
    do {
        char* iter = op_end;
        for (int i = 0; i < 3; i++)
        {
            errno = 0;
            version.arr[i] = strtoul(iter, &iter, 0 /* why would someone put hexadecimal in a version field? */);
            if (errno != 0)
            {
                *depend = NULL;
                *depend_version = (union package_version){};
                *version_cmp = VERSION_CMP_NONE;
                return;
            }
            if (i != 2 && *(iter++) != '.')
            {
                *depend = NULL;
                *depend_version = (union package_version){};
                *version_cmp = VERSION_CMP_NONE;
                return;
            }
        }
    } while(0);
    
    char* depend_str = malloc((op_location - depend_expr) + 1);
    memcpy(depend_str, depend_expr, (op_location - depend_expr));
    depend_str[op_location - depend_expr] = 0;
    
    *depend = depend_str;
    *depend_version = version;
    return;
}

bool do_version_cmp(int how, union package_version lhs, union package_version rhs)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    lhs.integer = __builtin_bswap32(lhs.integer);
    rhs.integer = __builtin_bswap32(rhs.integer);
#endif
    switch (how) {
        case VERSION_CMP_NONE: return true;
        case VERSION_CMP_LESS: return lhs.integer < rhs.integer;
        case VERSION_CMP_LESS_EQUAL: return lhs.integer <= rhs.integer;
        case VERSION_CMP_GREATER_EQUAL: return lhs.integer >= rhs.integer;
        case VERSION_CMP_GREATER: return lhs.integer > rhs.integer;
        case VERSION_CMP_EQUAL: return lhs.integer == rhs.integer;
        default: break;
    }
    abort();
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
    size_t nWritten = 0;
    while ((nWritten += fwrite(((char*)info) + nWritten, 1, sizeof(*info) + info->host_triplet_len - nWritten, file)) < sizeof(*info)+info->host_triplet_len)
    {
        if (ferror(file))
        {
            fprintf(stderr, "Unexpected error '%s' while reading pkginfo for package '%s'\n", strerror(errno), pkg_name);
            abort();
        }
    }
    fclose(file);
}

void foreach_package(bool installed_only, int(*cb)(package* pkg, struct pkginfo* info, void *userdata), void *userdata)
{
    DIR* iter = NULL;
    if (installed_only)
        iter = opendir(pkg_info_directory);
    else
        iter = opendir(recipes_directory);
    assert(iter);
    if (!iter)
        return;
    struct dirent* ent = NULL;
    while ((ent = readdir(iter)) != NULL)
    {
        if (ent->d_type != DT_REG)
            continue;
        const char* filename = ent->d_name;
        char* pkg_name = NULL;
        if (!installed_only)
        {
            char* end_recipe_name = strrchr(filename, '.');
            if (!end_recipe_name)
            {
                printf("Invalid recipe filename %s\n", filename);
                continue; // weirdness
            }
            size_t len_recipe_name = end_recipe_name - filename;
            pkg_name = memcpy(malloc(len_recipe_name+1), filename, len_recipe_name);
            pkg_name[len_recipe_name] = 0;
        }
        else
        {
            char* pkg_name_start = strchr(filename, '_');
            if (!pkg_name_start)
                continue; // probably just the README?
            pkg_name_start++;
            char* end_recipe_name = strrchr(pkg_name_start, '.');
            if (!end_recipe_name)
            {
                printf("Invalid pkginfo filename %s\n", pkg_name_start);
                continue; // weirdness
            }
            size_t len_recipe_name = end_recipe_name - pkg_name_start;
            pkg_name = memcpy(malloc(len_recipe_name+1), pkg_name_start, len_recipe_name);
            pkg_name[len_recipe_name] = 0;
        }
        package* pkg = get_package(pkg_name);
        struct pkginfo* info = read_package_info_ex(pkg_name, false, false);
        if (cb(pkg, info, userdata) != 0)
        {
            free(pkg_name);
            break;
        }
        free(pkg_name);
    }
    closedir(iter);
}