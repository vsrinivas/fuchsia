// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>

#include <pretty/sizes.h>
#include <task-utils/get.h>
#include <task-utils/walker.h>

#include "ps_internal.h"

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
static zx_status_t job_callback(void* ctx, int depth, zx_handle_t job, zx_koid_t koid,
                                zx_koid_t parent_koid) {
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

  // Put our entry pointer on the job stack so our descendants can find us.
  assert(depth < JOB_STACK_SIZE);
  job_stack[depth] = add_entry(&tasks, &e);
  return ZX_OK;
}

// Adds a process's information to |tasks|.
static zx_status_t process_callback(void* ctx, int depth, zx_handle_t process, zx_koid_t koid,
                                    zx_koid_t parent_koid) {
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
      task_entry_t* job = job_stack[i];
      job->pss_bytes += e.pss_bytes;
      job->private_bytes += e.private_bytes;
      // shared_bytes doesn't mean much as a sum, so leave it at zero.
    }
  }

  if (((ps_options_t*)ctx)->only_show_jobs) {
    return ZX_OK;
  }

  snprintf(e.koid_str, sizeof(e.koid_str), "%" PRIu64, koid);
  snprintf(e.parent_koid_str, sizeof(e.koid_str), "%" PRIu64, parent_koid);
  add_entry(&tasks, &e);

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
  if (!((ps_options_t*)ctx)->also_show_threads) {
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
  add_entry(&tasks, &e);
  return ZX_OK;
}

static void print_help(FILE* f) {
  fprintf(f, "Usage: ps [options]\n");
  fprintf(f, "Options:\n");
  fprintf(f, " -J             Only show jobs in the output\n");
  // -T for compatibility with linux ps
  fprintf(f, " -T             Include threads in the output\n");
  fprintf(f, " --units=?      Fix all sizes to the named unit\n");
  fprintf(f, "                where ? is one of [BkMGTPE]\n");
  fprintf(f, " --job=?        Show the given job and subjobs\n");
  fprintf(f, "                where ? is the job id.\n");
}

int main(int argc, char** argv) {
  ps_options_t options = {.also_show_threads = false, .only_show_jobs = false, .format_unit = 0};
  zx_status_t status;
  zx_handle_t target_job = ZX_HANDLE_INVALID;

  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (!strcmp(arg, "--help")) {
      print_help(stdout);
      return 0;
    }
    if (!strcmp(arg, "-J")) {
      options.only_show_jobs = true;
    } else if (!strcmp(arg, "-T")) {
      options.also_show_threads = true;
    } else if (!strncmp(arg, "--units=", sizeof("--units=") - 1)) {
      options.format_unit = arg[sizeof("--units=") - 1];
    } else if (!strncmp(arg, "--job=", sizeof("--job=") - 1)) {
      int jobid = atoi(arg + sizeof("--job=") - 1);
      zx_obj_type_t type;
      status = get_task_by_koid(jobid, &type, &target_job);
      if (status != ZX_OK) {
        fprintf(stderr, "ERROR: get_task_by_koid failed: %s (%d)\n", zx_status_get_string(status),
                status);
        return 1;
      }
      if (type != ZX_OBJ_TYPE_JOB) {
        fprintf(stderr, "ERROR: object with koid %d is not a job\n", jobid);
        return 1;
      }
    } else {
      fprintf(stderr, "Unknown option: %s\n", arg);
      print_help(stderr);
      return 1;
    }
  }

  int ret = 0;
  // If we have a target job, only walk the target subtree. Otherwise walk from root.
  if (target_job != ZX_HANDLE_INVALID) {
    status = walk_job_tree(target_job, job_callback, process_callback, thread_callback, &options);
    zx_handle_close(target_job);
  } else {
    status = walk_root_job_tree(job_callback, process_callback, thread_callback, &options);
  }
  if (status != ZX_OK) {
    fprintf(stderr, "WARNING: failed to walk the job tree: %s (%d)\n", zx_status_get_string(status),
            status);
    ret = 1;
  }
  print_table(&tasks, &options, stdout);
  free(tasks.entries);
  return ret;
}
