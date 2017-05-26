// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/status.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/exception.h>
#include <magenta/syscalls/object.h>
#include <pretty/sizes.h>
#include <task-utils/walker.h>

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_STATE_LEN (7 + 1) // +1 for trailing NUL

// A single task (job or process).
typedef struct {
    char type;                                     // 'j' (job), 'p' (process), or 't' (thread)
    char koid_str[sizeof("18446744073709551616")]; // 1<<64 + NUL
    int depth;
    char name[MX_MAX_NAME_LEN];
    char state_str[MAX_STATE_LEN];
    char private_bytes_str[MAX_FORMAT_SIZE_LEN];
    char shared_bytes_str[MAX_FORMAT_SIZE_LEN];
    char pss_bytes_str[MAX_FORMAT_SIZE_LEN];
} task_entry_t;

// An array of tasks.
typedef struct {
    task_entry_t* entries;
    size_t num_entries;
    size_t capacity; // allocation size
} task_table_t;

// Adds a task entry to the specified table. |*entry| is copied.
void add_entry(task_table_t* table, const task_entry_t* entry) {
    if (table->num_entries + 1 >= table->capacity) {
        size_t new_cap = table->capacity * 2;
        if (new_cap < 128) {
            new_cap = 128;
        }
        table->entries = realloc(table->entries, new_cap * sizeof(*entry));
        table->capacity = new_cap;
    }
    table->entries[table->num_entries++] = *entry;
}

// The array of tasks built by the callbacks.
static task_table_t tasks = {};

// Adds a job's information to |tasks|.
static mx_status_t job_callback(int depth, mx_handle_t job, mx_koid_t koid) {
    task_entry_t e = {.type = 'j', .depth = depth};
    mx_status_t status =
        mx_object_get_property(job, MX_PROP_NAME, e.name, sizeof(e.name));
    if (status != NO_ERROR) {
        return status;
    }
    snprintf(e.koid_str, sizeof(e.koid_str), "%" PRIu64, koid);
    add_entry(&tasks, &e);
    return NO_ERROR;
}

// Adds a process's information to |tasks|.
static mx_status_t process_callback(int depth, mx_handle_t process, mx_koid_t koid) {
    task_entry_t e = {.type = 'p', .depth = depth};
    mx_status_t status =
        mx_object_get_property(process, MX_PROP_NAME, e.name, sizeof(e.name));
    if (status != NO_ERROR) {
        return status;
    }
    mx_info_task_stats_t info;
    status = mx_object_get_info(
        process, MX_INFO_TASK_STATS, &info, sizeof(info), NULL, NULL);
    if (status == ERR_BAD_STATE) {
        // process has exited, but has not been destroyed
        info.mem_mapped_bytes = 0;
    } else if (status != NO_ERROR) {
        return status;
    }
    format_size(e.private_bytes_str, sizeof(e.private_bytes_str),
                info.mem_private_bytes);
    format_size(e.shared_bytes_str, sizeof(e.shared_bytes_str),
                info.mem_shared_bytes);
    format_size(e.pss_bytes_str, sizeof(e.pss_bytes_str),
                info.mem_private_bytes + info.mem_scaled_shared_bytes);
    snprintf(e.koid_str, sizeof(e.koid_str), "%" PRIu64, koid);
    add_entry(&tasks, &e);
    return NO_ERROR;
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
static mx_status_t thread_callback(int depth, mx_handle_t thread, mx_koid_t koid) {
    task_entry_t e = {.type = 't', .depth = depth};
    mx_status_t status =
        mx_object_get_property(thread, MX_PROP_NAME, e.name, sizeof(e.name));
    if (status != NO_ERROR) {
        return status;
    }
    mx_info_thread_t info;
    status = mx_object_get_info(thread, MX_INFO_THREAD, &info, sizeof(info), NULL, NULL);
    if (status != NO_ERROR) {
        return status;
    }
    // TODO: Print thread stack size in one of the memory usage fields?
    snprintf(e.koid_str, sizeof(e.koid_str), "%" PRIu64, koid);
    snprintf(e.state_str, sizeof(e.state_str), "%s", state_string(&info));
    add_entry(&tasks, &e);
    return NO_ERROR;
}

void print_header(int id_w, bool with_threads) {
    if (with_threads) {
        printf("%*s %7s %7s %7s %7s %s\n",
               -id_w, "TASK", "PSS", "PRIVATE", "SHARED", "STATE", "NAME");
    } else {
        printf("%*s %7s %7s %7s %s\n",
               -id_w, "TASK", "PSS", "PRIVATE", "SHARED", "NAME");
    }
}

// Prints the contents of |table| to stdout.
void print_table(task_table_t* table, bool with_threads) {
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
        if (with_threads) {
            printf("%*s %7s %7s %7s %7s %s\n",
                   -id_w, idbuf,
                   e->pss_bytes_str,
                   e->private_bytes_str,
                   e->shared_bytes_str,
                   e->state_str,
                   e->name);
        } else {
            printf("%*s %7s %7s %7s %s\n",
                   -id_w, idbuf,
                   e->pss_bytes_str,
                   e->private_bytes_str,
                   e->shared_bytes_str,
                   e->name);
        }
    }
    free(idbuf);
    print_header(id_w, with_threads);
}

static void print_help(FILE* f) {
    fprintf(f, "Usage: ps [options]\n");
    fprintf(f, "Options:\n");
    // -T for compatibility with linux ps
    fprintf(f, " -T  Include threads in the output\n");
}

int main(int argc, char** argv) {
    bool with_threads = false;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (!strcmp(arg, "--help")) {
            print_help(stdout);
            return 0;
        }
        if (!strcmp(arg, "-T")) {
            with_threads = true;
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg);
            print_help(stderr);
            return 1;
        }
    }

    int ret = 0;
    mx_status_t status = walk_root_job_tree(job_callback, process_callback,
                                            with_threads ? thread_callback : NULL);
    if (status != NO_ERROR) {
        fprintf(stderr, "WARNING: walk_root_job_tree failed: %s (%d)\n",
                mx_status_get_string(status), status);
        ret = 1;
    }
    print_table(&tasks, with_threads);
    free(tasks.entries);
    return ret;
}
