// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fdio/spawn.h>
#include <stdio.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s /path/to/binary [args...]\n", argv[0]);
    return 1;
  }

  zx_time_t start = zx_clock_get(ZX_CLOCK_MONOTONIC);

  zx_handle_t proc = ZX_HANDLE_INVALID;
  zx_status_t status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                  argv[1], (const char* const*)argv + 1, &proc);

  if (status != ZX_OK) {
    fprintf(stderr, "error: Failed to spawn '%s': %d (%s)\n", argv[1], status,
            zx_status_get_string(status));
    return 1;
  }

  status =
      zx_object_wait_one(proc, ZX_PROCESS_TERMINATED, ZX_TIME_INFINITE, NULL);

  zx_time_t stop = zx_clock_get(ZX_CLOCK_MONOTONIC);

  if (status != ZX_OK) {
    fprintf(stderr, "error: Failed to wait for process termination: %d (%s)\n",
            status, zx_status_get_string(status));
    return 1;
  }

  zx_info_process_t proc_info;
  status = zx_object_get_info(proc, ZX_INFO_PROCESS, &proc_info,
                              sizeof(proc_info), NULL, NULL);
  zx_handle_close(proc);

  if (status != ZX_OK) {
    fprintf(stderr, "error: Failed to get return code: %d (%s)\n", status,
            zx_status_get_string(status));
    return 1;
  }
  if (proc_info.return_code != 0) {
    fprintf(stderr, "error: %s exited with nonzero return code: %d\n", argv[1],
            (int)proc_info.return_code);
  }

  zx_duration_t delta = stop - start;
  uint64_t secs = delta / ZX_SEC(1);
  uint64_t usecs = (delta - secs * ZX_SEC(1)) / ZX_USEC(1);

  printf("real\t%ld.%06lds\n", secs, usecs);
  return proc_info.return_code;
}
