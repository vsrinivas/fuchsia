// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/status.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "format.h"
#include "processes.h"

// A single task (job or process).
typedef struct {
    char type; // 'j' (job) or 'p' (process)
    mx_koid_t koid;
    char koid_str[sizeof("18446744073709551616")]; // 1<<64 + NUL
    int depth;
    char name[MX_MAX_NAME_LEN];
    char mapped_bytes_str[MAX_FORMAT_SIZE_LEN];
    char allocated_bytes_str[MAX_FORMAT_SIZE_LEN];
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
    if (status != NO_ERROR) {
        return status;
    }
    format_size(e.mapped_bytes_str, sizeof(e.mapped_bytes_str),
                info.mem_mapped_bytes);
    format_size(e.allocated_bytes_str, sizeof(e.allocated_bytes_str),
                info.mem_committed_bytes);
    snprintf(e.koid_str, sizeof(e.koid_str), "%" PRIu64, koid);
    add_entry(&tasks, &e);
    return NO_ERROR;
}

void print_header(int id_w) {
    printf("%*s %7s %7s %s\n", -id_w, "TASK", "VIRT", "RES", "NAME");
}

// Prints the contents of |table| to stdout.
void print_table(task_table_t* table) {
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

    print_header(id_w);
    char* idbuf = (char*)malloc(id_w + 1);
    for (size_t i = 0; i < table->num_entries; i++) {
        const task_entry_t* e = table->entries + i;
        snprintf(idbuf, id_w + 1,
                 "%*s%c:%s", e->depth * 2, "", e->type, e->koid_str);
        printf("%*s %7s %7s %s\n",
               -id_w, idbuf,
               e->mapped_bytes_str, e->allocated_bytes_str, e->name);
    }
    print_header(id_w);
}

int main(int argc, char** argv) {
    int ret = 0;
    mx_status_t status = walk_process_tree(job_callback, process_callback);
    if (status != NO_ERROR) {
        fprintf(stderr, "WARNING: walk_process_tree failed: %s (%d)\n",
                mx_status_get_string(status), status);
        ret = 1;
    }
    print_table(&tasks);
    free(tasks.entries);
    return ret;
}
