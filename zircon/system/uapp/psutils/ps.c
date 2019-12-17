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

  // If we have a target job, only walk the target subtree. Otherwise walk from root.
  if (target_job != ZX_HANDLE_INVALID) {
    status = show_job_tree(target_job, &options);
    zx_handle_close(target_job);
  } else {
    status = show_all_jobs(&options);
  }
  if (status != ZX_OK) {
    fprintf(stderr, "WARNING: failed to walk the job tree: %s (%d)\n", zx_status_get_string(status),
            status);
    return 1;
  }
  return 0;
}
