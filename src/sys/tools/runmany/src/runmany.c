// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#define INBUFLEN (256)

zx_handle_t run_process(zx_handle_t job, int argc, const char* const* argv) {
  zx_handle_t process = ZX_HANDLE_INVALID;

  fdio_spawn_action_t actions[] = {
      // stdin needed to control jobs
      {.action = FDIO_SPAWN_ACTION_CLONE_FD,
       .fd = {.local_fd = STDOUT_FILENO, .target_fd = STDOUT_FILENO}},
      {.action = FDIO_SPAWN_ACTION_CLONE_FD,
       .fd = {.local_fd = STDERR_FILENO, .target_fd = STDERR_FILENO}},
  };
  size_t actions_count = sizeof(actions) / sizeof(actions[0]);

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_status_t status =
      fdio_spawn_etc(job,
                     FDIO_SPAWN_CLONE_JOB | FDIO_SPAWN_DEFAULT_LDSVC | FDIO_SPAWN_CLONE_NAMESPACE |
                         FDIO_SPAWN_CLONE_ENVIRON,
                     argv[0], argv, NULL, actions_count, actions, &process, err_msg);
  if (status != ZX_OK) {
    fprintf(stderr, "spawn failed: %s: %d\n", err_msg, status);
    return ZX_HANDLE_INVALID;
  }
  return process;
}

void kill_job(zx_handle_t job) {
  zx_status_t status = zx_task_kill(job);
  if (status != ZX_OK) {
    fprintf(stderr, "failed to kill job - err = %d\n", status);
  }
}

int main(int argc, const char* const* argv) {
  char inbuf[INBUFLEN];
  bool readmore = true;
  zx_status_t status;
  zx_handle_t job;

  int process_count;
  if (argc < 3 || (process_count = atoi(argv[1])) < 1) {
    printf("usage: %s <n> full-path-to-exec args...\n", argv[0]);
    return -1;
  }

  // set up jobs
  printf("starting %d processes\n", process_count);
  status = zx_job_create(zx_job_default(), 0, &job);
  if (status != ZX_OK) {
    fprintf(stderr, "zx_job_create failed - error %d\n", status);
    return -1;
  }
  for (int i = 0; i < process_count; i++) {
    zx_handle_t proc = run_process(job, argc - 2, argv + 2);
    if (!proc) {
      kill_job(job);
      printf("problem creating a process - shutting down\n");
      return -1;
    }
  }

  printf("enter q <return> to finish\n");

  while (readmore) {
    if (fgets(inbuf, INBUFLEN, stdin)) {
      if (feof(stdin) || strcmp("q\n", inbuf) == 0) {
        readmore = false;
      }
    }
  }

  kill_job(job);

  return 0;
}
