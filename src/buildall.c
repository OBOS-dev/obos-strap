/*
 * src/buildall.c
 *
 * Copyright (c) 2025 Omar Berrow
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <dirent.h>
#include <semaphore.h>
#include <unistd.h>

#include "package.h"
#include "tree.h"
#include "path.h"
#include "lock.h"

typedef struct package_list {
    struct package_node_ex *head, *tail;
    size_t nNodes;
} package_list;

typedef struct package_node_ex {
    struct package_node_ex *next, *prev;
    struct package_node *data;
} package_node_ex;

typedef struct package_node {
    const char* name;
    package* pkg;
    struct pkginfo* info;

    pthread_mutex_t dependants_mutex;
    package_list dependants;
    size_t missing_dep_count;

    // packages_zero_dependencies list
    struct package_node *next, *prev;

    RB_ENTRY(package_node) node;
} package_node;

static void add_dependant(package_node* dependency, package_node* dependant)
{
    package_list* list = &dependency->dependants;
    package_node_ex* node = calloc(1, sizeof(package_node_ex));
    node->data = dependant;

    if (!list->head)
        list->head = node;
    if (list->tail)
        list->tail->next = node;
    node->prev = list->tail;
    list->tail = node;
    list->nNodes++;
}

typedef RB_HEAD(package_tree, package_node) package_tree;
static package_tree packages;

// Packages with zero dependencies.
static struct {
    package_node *head, *tail;
    size_t nNodes;
} packages_zero_dependencies;

static int cmp_package_nodes(package_node* lhs, package_node* rhs)
{
    assert(lhs->name);
    assert(rhs->name);
    return strcmp(lhs->name, rhs->name);
}

RB_GENERATE_STATIC(package_tree, package_node, node, cmp_package_nodes);

static package_node* make_package_node(const char* pkg, package_node* parent)
{
    package_node what = {.name=pkg};
    package_node* found = RB_FIND(package_tree, &packages, &what);
    if (found)
        return found;

    package* pkg_parsed = get_package(pkg);
    if (!pkg_parsed)
        return NULL;
    struct pkginfo* info = read_package_info(pkg);

    package_node* node = calloc(1, sizeof(package_node));
    assert(node);
    pthread_mutex_init(&node->dependants_mutex, NULL);
    node->name = pkg;
    node->pkg = pkg_parsed;
    node->info = info;

    for (size_t i = 0; i < node->pkg->depends.cnt; i++)
    {
        char* dependency = node->pkg->depends.buf[i];
        if (parent && (strcmp(parent->name, dependency) == 0))
        {
            printf("%s: Recursive dependency %s<->%s. Dropping packages.", g_argv[0], parent->name, dependency);
            pthread_mutex_destroy(&node->dependants_mutex);
            free(node);
            return NULL;
        }
        what = (package_node){.name=dependency};
        package_node* dependency_node = make_package_node(dependency, node);
        node->missing_dep_count++;
        pthread_mutex_lock(&dependency_node->dependants_mutex);
        add_dependant(dependency_node, node);
        pthread_mutex_unlock(&dependency_node->dependants_mutex);

    }

    if (!node->pkg->depends.cnt)
    {
        if (!packages_zero_dependencies.head)
            packages_zero_dependencies.head = node;
        if (packages_zero_dependencies.tail)
            packages_zero_dependencies.tail->next = node;
        node->prev = packages_zero_dependencies.tail;
        packages_zero_dependencies.tail = node;
        packages_zero_dependencies.nNodes++;
    }

    RB_INSERT(package_tree, &packages, node);
    return node;
}

#if HAS_LIBCURL
#include <curl/curl.h>
#include <curl/easy.h>

typedef CURL *curl_handle;

#define cleanup_curl curl_easy_cleanup

curl_handle init_curl();
#else
#define init_curl() (-1)
#define cleanup_curl(c) (void)(c)
typedef int curl_handle;
#endif

bool build_pkg_internal(package* pkg, curl_handle curl_hnd, bool install, bool satisfy_dependencies);

sem_t awake_threads;

static bool build_pkg_and_dependants(package_node* node, curl_handle curl_hnd);

static void *build_thread(void* udata)
{
    curl_handle curl_hnd = init_curl();
    if (!curl_hnd)
    {
        printf("curl_easy_init failed\n");
        return (void*)(uintptr_t)false;
    }
    void* res = (void*)(uintptr_t)!!build_pkg_and_dependants(udata, curl_hnd);
    cleanup_curl(curl_hnd);
    sem_post(&awake_threads);
    return res;
}

static bool build_pkg_and_dependants(package_node* node, curl_handle curl_hnd)
{
    bool res = build_pkg_internal(node->pkg, curl_hnd, true, false);
    if (!res)
        return false;
    for (package_node_ex* curr = node->dependants.head; curr; )
    {
        package_node* const data = curr->data;
        curr = curr->next;
        if (!(--data->missing_dep_count))
        {
            int val = 0;
            sem_getvalue(&awake_threads, &val);
            if (val)
            {
                if (sem_trywait(&awake_threads) != 0)
                    goto no_thread;
                pthread_t thr_id = {};
                // We can now start a thread.
                int ec = pthread_create(&thr_id,
                                        NULL,
                                        build_thread, data);
                if (ec)
                {
                    perror("pthread_create");
                    fprintf(stderr, "Ignoring error.\n");
                    goto no_thread;
                }
                pthread_detach(thr_id);
                continue;
            }
            no_thread:
            res = build_pkg_and_dependants(data, curl_hnd);
            if (!res)
                break;
            continue;
        }
        (void)0;
    }
    return res;
}

void buildall()
{
    lock();

    // Discover packages for dependency graph.
    DIR* dir = opendir(recipes_directory);
    if (!dir)
    {
        perror("opendir");
        unlock();
        return;
    }

    struct dirent* ent = NULL;
    do {
        ent = readdir(dir);
        if (!ent)
            break;
        const char* pos_extension = strstr(ent->d_name, ".json");
        if (!pos_extension)
            continue;
        size_t pkg_len = pos_extension-ent->d_name;
        char* pkg_name = memcpy(malloc(pkg_len+1), ent->d_name, pkg_len);
        pkg_name[pkg_len] = 0;
        make_package_node(pkg_name, NULL);
    } while(ent);

    curl_handle curl_hnd = init_curl();
    if (!curl_hnd)
    {
        printf("curl_easy_init failed\n");
        unlock();
        return;
    }

    int nproc = sysconf(_SC_NPROCESSORS_ONLN);
    sem_init(&awake_threads, 0, nproc);
    sem_wait(&awake_threads);
    for (package_node* node = packages_zero_dependencies.head; node; )
    {
        build_pkg_and_dependants(node, curl_hnd);

        node = node->next;
    }
    sem_post(&awake_threads);
    int awake_thread_count = 0;
    do {
        // sem_wait(&awake_threads);
        sem_getvalue(&awake_threads, &awake_thread_count);
    } while(awake_thread_count != nproc);
    sem_destroy(&awake_threads);

    cleanup_curl(curl_hnd);

    unlock();
}
