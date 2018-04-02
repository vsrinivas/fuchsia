// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD - style license that can be
// found in the LICENSE file.

#include <fdio/io.h>
#include <fdio/private.h>
#include <launchpad/launchpad.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/device/pty.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#define INBUFLEN (256)

zx_handle_t run_process(zx_handle_t job, int argc, const char* const* argv) {
  zx_handle_t process = ZX_HANDLE_INVALID;

  launchpad_t* lp;
  launchpad_create(job, argv[0], &lp);
  launchpad_load_from_file(lp, argv[0]);
  launchpad_set_args(lp, argc, argv);
  launchpad_clone(lp, LP_CLONE_FDIO_NAMESPACE | LP_CLONE_ENVIRON);

  // stdin needed to control jobs
  launchpad_clone_fd(lp, STDOUT_FILENO, STDOUT_FILENO);
  launchpad_clone_fd(lp, STDERR_FILENO, STDERR_FILENO);

  const char* errmsg;
  zx_status_t status = launchpad_go(lp, &process, &errmsg);
  if (status < 0) {
    fprintf(stderr, "launchpad failed: %s: %d\n", errmsg, status);
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
      if (feof(stdin) ||  strcmp("q\n", inbuf) == 0) {
        readmore = false;
      }
    }
  }

  kill_job(job);

  return 0;
}
