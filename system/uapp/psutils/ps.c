// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/status.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/exception.h>
#include <magenta/syscalls/object.h>
#include <pretty/sizes.h>
#include <task-utils/walker.h>

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "resources.h"

#define MAX_STATE_LEN (7 + 1) // +1 for trailing NUL
#define MAX_KOID_LEN sizeof("18446744073709551616") // 1<<64 + NUL

static const char kJSONSchema[] =
    "{\n"
    "    \"type\": \"array\",\n"
    "    \"items\": {\n"
    "        \"type\": \"object\",\n"
    "        \"required\": [\"id\", \"type\", \"parent\", \"name\" ],\n"
    "        \"properties\": {\n"
    "            \"id\": { \"type\": \"string\" },\n"
    "            \"type\": { \"type\": \"string\" },\n"
    "            \"koid\": { \"type\": \"integer\" },\n"
    "            \"parent\": { \"type\": \"string\" },\n"
    "            \"name\": { \"type\": \"string\" },\n"
    "            \"private_bytes\": { \"type\": \"integer\" },\n"
    "            \"shared_bytes\": { \"type\": \"integer\" },\n"
    "            \"pss_bytes\": { \"type\": \"integer\" },\n"
    "            \"state\": { \"type\": \"string\" }\n"
    "        }\n"
    "    }\n"
    "}\n";

// A single task (job or process).
typedef struct {
    char type; // 'j' (job), 'p' (process), or 't' (thread)
    char koid_str[MAX_KOID_LEN];
    char parent_koid_str[MAX_KOID_LEN];
    int depth;
    char name[MX_MAX_NAME_LEN];
    char state_str[MAX_STATE_LEN];
    size_t pss_bytes;
    size_t private_bytes;
    size_t shared_bytes;
} task_entry_t;

// An array of tasks.
typedef struct {
    task_entry_t* entries;
    size_t num_entries;
    size_t capacity; // allocation size
} task_table_t;

// Adds a task entry to the specified table. |*entry| is copied.
// Returns a pointer to the new table entry.
task_entry_t* add_entry(task_table_t* table, const task_entry_t* entry) {
    if (table->num_entries + 1 >= table->capacity) {
        size_t new_cap = table->capacity * 2;
        if (new_cap < 128) {
            new_cap = 128;
        }
        table->entries = realloc(table->entries, new_cap * sizeof(*entry));
        table->capacity = new_cap;
    }
    table->entries[table->num_entries] = *entry;
    return table->entries + table->num_entries++;
}

// The array of tasks built by the callbacks.
static task_table_t tasks = {};

// The current stack of ancestor jobs, indexed by depth.
// process_callback may touch any entry whose depth is less that its own.
#define JOB_STACK_SIZE 128
static task_entry_t* job_stack[JOB_STACK_SIZE];

// Adds a job's information to |tasks|.
static mx_status_t job_callback(void* unused_ctx, int depth, mx_handle_t job,
                                mx_koid_t koid, mx_koid_t parent_koid) {
    task_entry_t e = {.type = 'j', .depth = depth};
    mx_status_t status =
        mx_object_get_property(job, MX_PROP_NAME, e.name, sizeof(e.name));
    if (status != MX_OK) {
        // This will abort walk_job_tree(), so we don't need to worry
        // about job_stack.
        return status;
    }
    snprintf(e.koid_str, sizeof(e.koid_str), "%" PRIu64, koid);
    snprintf(e.parent_koid_str, sizeof(e.koid_str), "%" PRIu64, parent_koid);

    // Put our entry pointer on the job stack so our descendants can find us.
    assert(depth < JOB_STACK_SIZE);
    job_stack[depth] = add_entry(&tasks, &e);
    return MX_OK;
}

// Adds a process's information to |tasks|.
static mx_status_t process_callback(void* unused_ctx, int depth,
                                    mx_handle_t process,
                                    mx_koid_t koid, mx_koid_t parent_koid) {
    task_entry_t e = {.type = 'p', .depth = depth};
    mx_status_t status =
        mx_object_get_property(process, MX_PROP_NAME, e.name, sizeof(e.name));
    if (status != MX_OK) {
        return status;
    }
    mx_info_task_stats_t info;
    status = mx_object_get_info(
        process, MX_INFO_TASK_STATS, &info, sizeof(info), NULL, NULL);
    if (status == MX_ERR_BAD_STATE) {
        // Process has exited, but has not been destroyed.
        // Default to zero for all sizes.
    } else if (status != MX_OK) {
        return status;
    } else {
        e.private_bytes = info.mem_private_bytes;
        e.shared_bytes = info.mem_shared_bytes;
        e.pss_bytes = info.mem_private_bytes + info.mem_scaled_shared_bytes;
    }
    snprintf(e.koid_str, sizeof(e.koid_str), "%" PRIu64, koid);
    snprintf(e.parent_koid_str, sizeof(e.koid_str), "%" PRIu64, parent_koid);
    add_entry(&tasks, &e);

    // Update our ancestor jobs.
    assert(depth > 0);
    assert(depth < JOB_STACK_SIZE);
    for (int i = 0; i < depth; i++) {
        task_entry_t* job = job_stack[i];
        job->pss_bytes += info.mem_private_bytes + info.mem_scaled_shared_bytes;
        job->private_bytes += info.mem_private_bytes;
        // shared_bytes doesn't mean much as a sum, so leave it at zero.
    }

    return MX_OK;
}

// Return text representation of thread state.
static const char* state_string(const mx_info_thread_t* info) {
    if (info->wait_exception_port_type != MX_EXCEPTION_PORT_TYPE_NONE) {
        return "excp";
    } else {
        switch (info->state) {
        case MX_THREAD_STATE_NEW:
            return "new";
        case MX_THREAD_STATE_RUNNING:
            return "running";
        case MX_THREAD_STATE_SUSPENDED:
            return "susp";
        case MX_THREAD_STATE_BLOCKED:
            return "blocked";
        case MX_THREAD_STATE_DYING:
            return "dying";
        case MX_THREAD_STATE_DEAD:
            return "dead";
        default:
            return "???";
        }
    }
}

// Adds a thread's information to |tasks|.
static mx_status_t thread_callback(void* unused_ctx, int depth,
                                   mx_handle_t thread,
                                   mx_koid_t koid, mx_koid_t parent_koid) {
    task_entry_t e = {.type = 't', .depth = depth};
    mx_status_t status =
        mx_object_get_property(thread, MX_PROP_NAME, e.name, sizeof(e.name));
    if (status != MX_OK) {
        return status;
    }
    mx_info_thread_t info;
    status = mx_object_get_info(thread, MX_INFO_THREAD, &info, sizeof(info),
                                NULL, NULL);
    if (status != MX_OK) {
        return status;
    }
    // TODO: Print thread stack size in one of the memory usage fields?
    snprintf(e.koid_str, sizeof(e.koid_str), "%" PRIu64, koid);
    snprintf(e.parent_koid_str, sizeof(e.koid_str), "%" PRIu64, parent_koid);
    snprintf(e.state_str, sizeof(e.state_str), "%s", state_string(&info));
    add_entry(&tasks, &e);
    return MX_OK;
}

static void print_header(int id_w, bool with_threads) {
    if (with_threads) {
        printf("%*s %7s %7s %7s %7s %s\n",
               -id_w, "TASK", "PSS", "PRIVATE", "SHARED", "STATE", "NAME");
    } else {
        printf("%*s %7s %7s %7s %s\n",
               -id_w, "TASK", "PSS", "PRIVATE", "SHARED", "NAME");
    }
}

// The |unit| param to pass to format_size_fixed().
static char format_unit = 0;

// Prints the contents of |table| to stdout.
static void print_table(task_table_t* table, bool with_threads) {
    if (table->num_entries == 0) {
        return;
    }

    // Find the width of the id column; the rest are fixed or don't matter.
    int id_w = 0;
    for (size_t i = 0; i < table->num_entries; i++) {
        const task_entry_t* e = table->entries + i;
        // Indentation + type + : + koid
        int w = 2 * e->depth + 2 + strlen(e->koid_str);
        if (w > id_w) {
            id_w = w;
        }
    }

    print_header(id_w, with_threads);
    char* idbuf = (char*)malloc(id_w + 1);
    for (size_t i = 0; i < table->num_entries; i++) {
        const task_entry_t* e = table->entries + i;
        if (e->type == 't' && !with_threads) {
            continue;
        }
        snprintf(idbuf, id_w + 1,
                 "%*s%c:%s", e->depth * 2, "", e->type, e->koid_str);

        // Format the size fields for entry types that need them.
        char pss_bytes_str[MAX_FORMAT_SIZE_LEN] = {};
        char private_bytes_str[MAX_FORMAT_SIZE_LEN] = {};
        if (e->type == 'j' || e->type == 'p') {
            format_size_fixed(pss_bytes_str, sizeof(pss_bytes_str),
                              e->pss_bytes, format_unit);
            format_size_fixed(private_bytes_str, sizeof(private_bytes_str),
                              e->private_bytes, format_unit);
        }
        char shared_bytes_str[MAX_FORMAT_SIZE_LEN] = {};
        if (e->type == 'p') {
            format_size_fixed(shared_bytes_str, sizeof(shared_bytes_str),
                              e->shared_bytes, format_unit);
        }

        if (with_threads) {
            printf("%*s %7s %7s %7s %7s %s\n",
                   -id_w, idbuf,
                   pss_bytes_str,
                   private_bytes_str,
                   shared_bytes_str,
                   e->state_str,
                   e->name);
        } else {
            printf("%*s %7s %7s %7s %s\n",
                   -id_w, idbuf,
                   pss_bytes_str,
                   private_bytes_str,
                   shared_bytes_str,
                   e->name);
        }
    }
    free(idbuf);
    print_header(id_w, with_threads);
}

static void print_kernel_json(const char* name, const char* parent,
                              uint64_t size_bytes) {
    printf("  {"
           "\"id\": \"kernel/%s\", "
           "\"type\": \"kernel\", "
           "\"parent\": \"%s\", "
           "\"name\": \"%s\", "
           "\"size_bytes\": %zu"
           "},\n",
           name,
           parent,
           name,
           size_bytes);
}

// Update kJSONSchema if you change the output format.
static mx_status_t print_json(task_table_t* table) {
    mx_handle_t root_resource;
    mx_status_t s = get_root_resource(&root_resource);
    if (s != MX_OK) {
        return s;
    }
    mx_info_kmem_stats_t stats;
    s = mx_object_get_info(
        root_resource, MX_INFO_KMEM_STATS, &stats, sizeof(stats), NULL, NULL);
    mx_handle_close(root_resource);
    if (s != MX_OK) {
        fprintf(stderr, "ERROR: MX_INFO_KMEM_STATS returns %d (%s)\n",
                s, mx_status_get_string(s));
        return s;
    }

    printf("[\n");

    // Kernel memory info.
    // TODO(dbort): Add a non-JSON way to see these, although
    // they're also available via |kstats -m|.
    print_kernel_json("physmem", "", stats.total_bytes);
    print_kernel_json("free", "kernel/physmem", stats.free_bytes);
    print_kernel_json("vmo", "kernel/physmem", stats.vmo_bytes);
    print_kernel_json("heap", "kernel/physmem", stats.total_heap_bytes);
    print_kernel_json("heap/allocated", "kernel/heap",
                      stats.total_heap_bytes - stats.free_heap_bytes);
    print_kernel_json("heap/free", "kernel/heap", stats.free_heap_bytes);
    print_kernel_json("wired", "kernel/physmem", stats.wired_bytes);
    print_kernel_json("mmu", "kernel/physmem", stats.mmu_overhead_bytes);
    print_kernel_json("other", "kernel/physmem", stats.other_bytes);

    // Task info.
    for (size_t i = 0; i < table->num_entries; i++) {
        const task_entry_t* e = table->entries + i;
        const char* delimiter = i + 1 == table->num_entries ? "" : ",";

        if (e->type == 'j') {
            char parent[64];
            if (strcmp(e->parent_koid_str, "0") == 0) {
                // This is the root job, which we treat as a child
                // of the vmo node.
                snprintf(parent, sizeof(parent), "kernel/vmo");
            } else {
                snprintf(parent, sizeof(parent), "j/%s", e->parent_koid_str);
            }
            printf("  {"
                   "\"id\": \"%c/%s\", "
                   "\"type\": \"%c\", "
                   "\"koid\": %s, "
                   "\"parent\": \"%s\", "
                   "\"name\": \"%s\", "
                   "\"private_bytes\": %zu, "
                   "\"pss_bytes\": %zu"
                   "}%s\n",
                   e->type, e->koid_str,
                   e->type,
                   e->koid_str,
                   parent,
                   e->name,
                   e->private_bytes,
                   e->pss_bytes,
                   delimiter);
        } else if (e->type == 'p') {
            printf("  {"
                   "\"id\": \"%c/%s\", "
                   "\"type\": \"%c\", "
                   "\"koid\": %s, "
                   "\"parent\": \"j/%s\", "
                   "\"name\": \"%s\", "
                   "\"private_bytes\": %zu, "
                   "\"shared_bytes\": %zu, "
                   "\"pss_bytes\": %zu"
                   "}%s\n",
                   e->type, e->koid_str,
                   e->type,
                   e->koid_str,
                   e->parent_koid_str,
                   e->name,
                   e->private_bytes,
                   e->shared_bytes,
                   e->pss_bytes,
                   delimiter);
        } else if (e->type == 't') {
            printf("  {"
                   "\"id\": \"%c/%s\", "
                   "\"type\": \"%c\", "
                   "\"koid\": %s, "
                   "\"parent\": \"p/%s\", "
                   "\"name\": \"%s\", "
                   "\"state\": \"%s\""
                   "}%s\n",
                   e->type, e->koid_str,
                   e->type,
                   e->koid_str,
                   e->parent_koid_str,
                   e->name,
                   e->state_str,
                   delimiter);
        } else {
            fprintf(stderr, "ERROR: unknown task type: %c\n", e->type);
        }
    }

    printf("]\n");

    return MX_OK;
}

static void print_help(FILE* f) {
    fprintf(f, "Usage: ps [options]\n");
    fprintf(f, "Options:\n");
    // -T for compatibility with linux ps
    fprintf(f, " -T             Include threads in the output\n");
    fprintf(f, " --json         Print output in JSON\n");
    fprintf(f, " --json-schema  Print a schema for the JSON output format\n");
    fprintf(f, " --units=?      Fix all sizes to the named unit\n");
    fprintf(f, "                where ? is one of [BkMGTPE]\n");
}

int main(int argc, char** argv) {
    bool with_threads = false;
    bool use_json = false;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (!strcmp(arg, "--help")) {
            print_help(stdout);
            return 0;
        }
        if (!strcmp(arg, "--json-schema")) {
            printf(kJSONSchema);
            return 0;
        }
        if (!strcmp(arg, "-T")) {
            with_threads = true;
        } else if (!strcmp(arg, "--json")) {
            use_json = true;
        } else if (!strncmp(arg, "--units=", sizeof("--units=") - 1)) {
            format_unit = arg[sizeof("--units=") - 1];
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg);
            print_help(stderr);
            return 1;
        }
    }

    int ret = 0;
    mx_status_t status =
        walk_root_job_tree(job_callback, process_callback,
                           with_threads ? thread_callback : NULL, NULL);
    if (status != MX_OK) {
        fprintf(stderr, "WARNING: walk_root_job_tree failed: %s (%d)\n",
                mx_status_get_string(status), status);
        ret = 1;
    }
    if (use_json) {
        ret = print_json(&tasks);
    } else {
        print_table(&tasks, with_threads);
    }
    free(tasks.entries);
    return ret;
}
