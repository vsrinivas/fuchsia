// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/launchpad.h>
#include <magenta/syscalls.h>
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

  char **args = argv + 1;

  mx_handle_t handle = launchpad_launch_mxio(argv[1], argc - 1, args);
  if (handle < 0) {
    fprintf(stderr, "Failed to launch %s: %d\n", argv[1], handle);
    return 1;
  }
  mx_status_t status =
      mx_handle_wait_one(handle, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL);

  gettimeofday(&endtimeval, NULL);

  if (status != NO_ERROR) {
    fprintf(stderr, "Failed to wait for process exiting %s: %d\n", argv[1],
           status);
    return 1;
  }

  mx_info_process_t proc_info;
  mx_ssize_t info_status = mx_object_get_info(handle, MX_INFO_PROCESS,
                                              sizeof(proc_info.rec), &proc_info,
                                              sizeof(proc_info));
  mx_handle_close(handle);

  if (info_status != sizeof(proc_info)) {
    fprintf(stderr, "Failed to get process return code %s: %ld\n", argv[1],
           info_status);
    return 1;
  }
  if (proc_info.rec.return_code != 0) {
    fprintf(stderr, "%s exited with nonzero status: %d\n", argv[1],
           proc_info.rec.return_code);
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
  return proc_info.rec.return_code;
}
