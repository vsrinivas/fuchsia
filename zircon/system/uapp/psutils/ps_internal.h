// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UAPP_PSUTILS_PS_INTERNAL_H_
#define ZIRCON_SYSTEM_UAPP_PSUTILS_PS_INTERNAL_H_

#include <stdio.h>
#include <zircon/types.h>

// These are only exposed for testing.

#define MAX_STATE_LEN (7 + 1)                        // +1 for trailing NUL
#define MAX_KOID_LEN sizeof("18446744073709551616")  // 1<<64 + NUL

// A single task (job or process).
typedef struct {
  char type;  // 'j' (job), 'p' (process), or 't' (thread)
  char koid_str[MAX_KOID_LEN];
  char parent_koid_str[MAX_KOID_LEN];
  int depth;
  char name[ZX_MAX_NAME_LEN];
  char state_str[MAX_STATE_LEN];
  size_t pss_bytes;
  size_t private_bytes;
  size_t shared_bytes;
} task_entry_t;

// An array of tasks.
typedef struct {
  task_entry_t* entries;
  size_t num_entries;
  size_t capacity;  // allocation size
} task_table_t;

// Controls what is shown.
typedef struct {
  bool also_show_threads;
  bool only_show_jobs;
  char format_unit;
} ps_options_t;

// Prints the contents of |table| to |out|.
void print_table(task_table_t* table, const ps_options_t* options, FILE* out);

void print_header(int id_w, const ps_options_t* options, FILE* out);

// Print to stdout all jobs in the system.
zx_status_t show_all_jobs(const ps_options_t* options);

// Print to stdout all jobs/processes/threads under the given job.
zx_status_t show_job_tree(zx_handle_t target_job, const ps_options_t* options);

#endif  // ZIRCON_SYSTEM_UAPP_PSUTILS_PS_INTERNAL_H_
