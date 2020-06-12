// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/syscalls.h>

#include <task-utils/walker.h>

typedef struct {
  zx_koid_t task_id;
  zx_signals_t clear_mask;
  zx_signals_t set_mask;
} signal_target_t;

static zx_status_t callback(void* ctx, int depth, zx_handle_t process, zx_koid_t koid,
                            zx_koid_t parent_koid) {
  signal_target_t* sigt = (signal_target_t*)ctx;
  if (koid == sigt->task_id) {
    zx_status_t status = zx_object_signal(process, sigt->clear_mask, sigt->set_mask);
    if (status == ZX_OK) {
      printf("signalled process %ld\n", sigt->task_id);
    } else {
      printf("something wrong with signalling process %ld, status: %d\n", sigt->task_id, status);
    }
    // found and signaled the task - abort the search
    return ZX_ERR_STOP;
  }
  return ZX_OK;
}

static void usage(const char* progname) {
  fprintf(stderr, "usage: %s <task id> signal-number <set|clear>\n", progname);
  fprintf(stderr, "signal number is in the range [0-7] and refers to ZX_USER_SIGNAL_[0-7] bits\n");
  fprintf(stderr,
          "set|clear indicates wether the signal is added to the set_mask (set) or to the "
          "clear_mask (clear)\n");
}

int main(int argc, const char** argv) {
  if (argc < 4) {
    usage(argv[0]);
    return -1;
  }

  char* endptr;
  zx_koid_t task_id = strtol(argv[1], &endptr, 10);
  if (*endptr != '\0') {
    fprintf(stderr, "\"%s\" is not a valid task id\n", argv[1]);
    return -1;
  }

  long signal = strtol(argv[2], &endptr, 10);
  if (*endptr != '\0' || signal < 0 || signal > 7) {
    fprintf(stderr, "\"%s\" is not a valid signal number\n", argv[2]);
    usage(argv[0]);
    return -1;
  }

  static const zx_signals_t user_signal[8] = {ZX_USER_SIGNAL_0, ZX_USER_SIGNAL_1, ZX_USER_SIGNAL_2,
                                              ZX_USER_SIGNAL_3, ZX_USER_SIGNAL_4, ZX_USER_SIGNAL_5,
                                              ZX_USER_SIGNAL_6, ZX_USER_SIGNAL_7};

  bool is_up;
  if (strcmp(argv[3], "set") == 0) {
    is_up = true;
  } else if (strcmp(argv[3], "clear") == 0) {
    is_up = false;
  } else {
    usage(argv[0]);
    return -1;
  }

  signal_target_t sigt = {.task_id = task_id,
                          .clear_mask = is_up ? 0 : user_signal[signal],
                          .set_mask = is_up ? user_signal[signal] : 0};

  zx_status_t status = walk_root_job_tree(callback, callback, NULL, &sigt);
  if (status == ZX_OK) {
    fprintf(stderr, "task %lu not found\n", task_id);
    return -1;
  }

  return 0;
}
