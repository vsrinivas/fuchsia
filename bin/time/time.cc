// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/launchpad.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <magenta/types.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "correct usage: %s /path/to/binary\n", argv[0]);
    return 1;
  }

  timeval starttimeval, endtimeval;
  gettimeofday(&starttimeval, NULL);

  launchpad_t* lp;
  // Use the default job.
  launchpad_create(MX_HANDLE_INVALID, argv[1], &lp);
  launchpad_load_from_file(lp, argv[1]);
  launchpad_clone(lp, LP_CLONE_ALL);
  launchpad_set_args(lp, argc - 1, argv + 1);

  mx_handle_t proc = MX_HANDLE_INVALID;
  const char* errmsg = NULL;
  mx_status_t status = launchpad_go(lp, &proc, &errmsg);
  if (status != MX_OK) {
    fprintf(stderr, "Failed to launch %s: %d: %s\n", argv[1], status, errmsg);
    return 1;
  }
  status =
      mx_object_wait_one(proc, MX_PROCESS_TERMINATED, MX_TIME_INFINITE, NULL);

  gettimeofday(&endtimeval, NULL);

  if (status != MX_OK) {
    fprintf(stderr, "Failed to wait for process exiting %s: %d\n", argv[1],
           status);
    return 1;
  }

  mx_info_process_t proc_info;
  status = mx_object_get_info(proc, MX_INFO_PROCESS, &proc_info,
                              sizeof(proc_info), nullptr, nullptr);
  mx_handle_close(proc);

  if (status != MX_OK) {
    fprintf(stderr, "Failed to get process return code %s: %d\n", argv[1],
           status);
    return 1;
  }
  if (proc_info.return_code != 0) {
    fprintf(stderr, "%s exited with nonzero status: %d\n", argv[1],
           proc_info.return_code);
  }
  suseconds_t usecdiff =
      endtimeval.tv_usec >= starttimeval.tv_usec
          ? endtimeval.tv_usec - starttimeval.tv_usec
          : 1000000 + endtimeval.tv_usec - starttimeval.tv_usec;
  time_t secdiff = endtimeval.tv_sec - starttimeval.tv_sec;
  if (endtimeval.tv_usec < starttimeval.tv_usec) {
    secdiff--;
  }
  printf("real\t%ld.%06lds\n", secdiff, usecdiff);
  return proc_info.return_code;
}
