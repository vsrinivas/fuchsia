// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ps_internal.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include <pretty/sizes.h>

#include <task-utils/get.h>
#include <task-utils/walker.h>

#define JOB_STACK_SIZE 128

void print_header(int id_w, const ps_options_t* options, FILE* out) {
  if (options->also_show_threads) {
    fprintf(out, "%*s %7s %7s %7s %7s %s\n", -id_w, "TASK", "PSS", "PRIVATE", "SHARED", "STATE",
            "NAME");
  } else if (options->only_show_jobs) {
    fprintf(out, "%*s %7s %7s %7s %s\n", -id_w, "TASK", "PSS", "PRIVATE", "STATE", "NAME");
  } else {
    fprintf(out, "%*s %7s %7s %7s %7s %s\n", -id_w, "TASK", "PSS", "PRIVATE", "SHARED", "STATE",
            "NAME");
  }
}

void print_table(task_table_t* table, const ps_options_t* options, FILE* out) {
  if (table->num_entries == 0) {
    return;
  }

  // Find the width of the id column; the rest are fixed or don't matter.
  int id_w = 0;
  for (size_t i = 0; i < table->num_entries; i++) {
    const task_entry_t* e = table->entries + i;
    // Indentation + type + : + space + koid
    int w = 2 * e->depth + 3 + strlen(e->koid_str);
    if (w > id_w) {
      id_w = w;
    }
  }

  print_header(id_w, options, out);
  char* idbuf = (char*)malloc(id_w + 1);
  for (size_t i = 0; i < table->num_entries; i++) {
    const task_entry_t* e = table->entries + i;
    if (e->type == 't' && !options->also_show_threads) {
      continue;
    }
    snprintf(idbuf, id_w + 1, "%*s%c: %s", e->depth * 2, "", e->type, e->koid_str);

    // Format the size fields for entry types that need them.
    char pss_bytes_str[MAX_FORMAT_SIZE_LEN] = {};
    char private_bytes_str[MAX_FORMAT_SIZE_LEN] = {};
    if (e->type == 'j' || e->type == 'p') {
      format_size_fixed(pss_bytes_str, sizeof(pss_bytes_str), e->pss_bytes, options->format_unit);
      format_size_fixed(private_bytes_str, sizeof(private_bytes_str), e->private_bytes,
                        options->format_unit);
    }
    char shared_bytes_str[MAX_FORMAT_SIZE_LEN] = {};
    if (e->type == 'p') {
      format_size_fixed(shared_bytes_str, sizeof(shared_bytes_str), e->shared_bytes,
                        options->format_unit);
    }

    if (options->also_show_threads) {
      fprintf(out, "%*s %7s %7s %7s %7s %s\n", -id_w, idbuf, pss_bytes_str, private_bytes_str,
              shared_bytes_str, e->state_str, e->name);
    } else if (options->only_show_jobs) {
      fprintf(out, "%*s %7s %7s %7s %s\n", -id_w, idbuf, pss_bytes_str, private_bytes_str,
              e->state_str, e->name);
    } else {
      fprintf(out, "%*s %7s %7s %7s %7s %s\n", -id_w, idbuf, pss_bytes_str, private_bytes_str,
              shared_bytes_str, e->state_str, e->name);
    }
  }
  free(idbuf);
  print_header(id_w, options, out);
}

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

// Data object passed through callbacks.
typedef struct context {
  const ps_options_t* options;

  // Seen tasks.
  task_table_t tasks;

  // The current stack of ancestor jobs, indexed by depth.
  // process_callback may touch any entry whose depth is less that its own.
  size_t job_stack[JOB_STACK_SIZE];
} context_t;

// Adds a job's information to |tasks|.
static zx_status_t job_callback(void* ctx, int depth, zx_handle_t job, zx_koid_t koid,
                                zx_koid_t parent_koid) {
  context_t* context = ctx;

  task_entry_t e = {.type = 'j', .depth = depth};
  zx_status_t status = zx_object_get_property(job, ZX_PROP_NAME, e.name, sizeof(e.name));
  if (status != ZX_OK) {
    // This will abort walk_job_tree(), so we don't need to worry
    // about job_stack.
    return status;
  }

  zx_info_job_t info;
  status = zx_object_get_info(job, ZX_INFO_JOB, &info, sizeof(info), NULL, NULL);
  if (status != ZX_OK) {
    return status;
  }
  if (info.kill_on_oom) {
    snprintf(e.state_str, sizeof(e.state_str), "killoom");
  }

  snprintf(e.koid_str, sizeof(e.koid_str), "%" PRIu64, koid);
  snprintf(e.parent_koid_str, sizeof(e.koid_str), "%" PRIu64, parent_koid);

  // Put our entry index on the job stack so our descendants can find us.
  assert(depth < JOB_STACK_SIZE);
  task_entry_t* new_entry = add_entry(&context->tasks, &e);
  task_entry_t* base = context->tasks.entries;
  context->job_stack[depth] = new_entry - base;
  return ZX_OK;
}

// Adds a process's information to |tasks|.
static zx_status_t process_callback(void* ctx, int depth, zx_handle_t process, zx_koid_t koid,
                                    zx_koid_t parent_koid) {
  context_t* context = ctx;

  task_entry_t e = {.type = 'p', .depth = depth};
  zx_status_t status = zx_object_get_property(process, ZX_PROP_NAME, e.name, sizeof(e.name));
  if (status != ZX_OK) {
    return status;
  }
  zx_info_task_stats_t info;
  status = zx_object_get_info(process, ZX_INFO_TASK_STATS, &info, sizeof(info), NULL, NULL);
  if (status == ZX_ERR_BAD_STATE) {
    // Process has exited, but has not been destroyed.
    // Default to zero for all sizes.
  } else if (status != ZX_OK) {
    return status;
  } else {
    e.private_bytes = info.mem_private_bytes;
    e.shared_bytes = info.mem_shared_bytes;
    e.pss_bytes = info.mem_private_bytes + info.mem_scaled_shared_bytes;

    // Update our ancestor jobs.
    assert(depth > 0);
    assert(depth < JOB_STACK_SIZE);
    for (int i = 0; i < depth; i++) {
      task_entry_t* job = &context->tasks.entries[context->job_stack[i]];
      job->pss_bytes += e.pss_bytes;
      job->private_bytes += e.private_bytes;
      // shared_bytes doesn't mean much as a sum, so leave it at zero.
    }
  }

  if (context->options->only_show_jobs) {
    return ZX_OK;
  }

  snprintf(e.koid_str, sizeof(e.koid_str), "%" PRIu64, koid);
  snprintf(e.parent_koid_str, sizeof(e.koid_str), "%" PRIu64, parent_koid);
  add_entry(&context->tasks, &e);

  return ZX_OK;
}

// Return text representation of thread state.
static const char* state_string(const zx_info_thread_t* info) {
  if (info->wait_exception_channel_type != ZX_EXCEPTION_CHANNEL_TYPE_NONE) {
    return "excp";
  } else {
    switch (ZX_THREAD_STATE_BASIC(info->state)) {
      case ZX_THREAD_STATE_NEW:
        return "new";
      case ZX_THREAD_STATE_RUNNING:
        return "running";
      case ZX_THREAD_STATE_SUSPENDED:
        return "susp";
      case ZX_THREAD_STATE_BLOCKED:
        return "blocked";
      case ZX_THREAD_STATE_DYING:
        return "dying";
      case ZX_THREAD_STATE_DEAD:
        return "dead";
      default:
        return "???";
    }
  }
}

// Adds a thread's information to |tasks|.
static zx_status_t thread_callback(void* ctx, int depth, zx_handle_t thread, zx_koid_t koid,
                                   zx_koid_t parent_koid) {
  context_t* context = ctx;

  if (!context->options->also_show_threads) {
    // TODO(cpu): Should update ancestor process with number of threads.
    return ZX_OK;
  }

  task_entry_t e = {.type = 't', .depth = depth};
  zx_status_t status = zx_object_get_property(thread, ZX_PROP_NAME, e.name, sizeof(e.name));
  if (status != ZX_OK) {
    return status;
  }
  zx_info_thread_t info;
  status = zx_object_get_info(thread, ZX_INFO_THREAD, &info, sizeof(info), NULL, NULL);
  if (status != ZX_OK) {
    return status;
  }
  // TODO: Print thread stack size in one of the memory usage fields?
  snprintf(e.koid_str, sizeof(e.koid_str), "%" PRIu64, koid);
  snprintf(e.parent_koid_str, sizeof(e.koid_str), "%" PRIu64, parent_koid);
  snprintf(e.state_str, sizeof(e.state_str), "%s", state_string(&info));
  add_entry(&context->tasks, &e);
  return ZX_OK;
}

zx_status_t show_all_jobs(const ps_options_t* options) {
  context_t context = {.options = options};

  zx_status_t status =
      walk_root_job_tree(job_callback, process_callback, thread_callback, &context);
  if (status != ZX_OK) {
    return status;
  }
  print_table(&context.tasks, options, stdout);
  free(context.tasks.entries);
  return ZX_OK;
}

zx_status_t show_job_tree(zx_handle_t target_job, const ps_options_t* options) {
  context_t context = {.options = options};

  zx_status_t status =
      walk_job_tree(target_job, job_callback, process_callback, thread_callback, &context);
  if (status != ZX_OK) {
    return status;
  }
  print_table(&context.tasks, options, stdout);
  free(context.tasks.entries);
  return ZX_OK;
}
