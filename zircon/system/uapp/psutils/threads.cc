// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <inspector/inspector.h>
#include <task-utils/get.h>
#include <task-utils/dump-threads.h>

void print_error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, "ERROR: ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);
}

void print_zx_error(zx_status_t status, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, "ERROR: ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, ": %d(%s)", status, zx_status_get_string(status));
  fprintf(stderr, "\n");
  va_end(args);
}

void usage(FILE* f) {
  fprintf(f, "Usage: threads [options] pid\n");
  fprintf(f, "Options:\n");
  fprintf(f, "  -v[n] = set verbosity level to N\n");
}

int main(int argc, char** argv) {
  zx_status_t status;
  zx_koid_t pid = ZX_KOID_INVALID;

  int i;
  uint8_t verbosity_level = 0;
  for (i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (arg[0] == '-') {
      if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
        usage(stdout);
        return 0;
      } else if (strncmp(arg, "-v", 2) == 0) {
        if (arg[2] != '\0') {
          verbosity_level = static_cast<uint8_t>(atoi(arg + 2));
        } else {
          verbosity_level = 1;
        }
      } else {
        usage(stderr);
        return 1;
      }
    } else {
      break;
    }
  }
  if (i == argc || i + 1 != argc) {
    usage(stderr);
    return 1;
  }
  char* endptr;
  const char* pidstr = argv[i];
  pid = strtoull(pidstr, &endptr, 0);
  if (!(pidstr[0] != '\0' && *endptr == '\0')) {
    fprintf(stderr, "ERROR: invalid pid: %s", pidstr);
    return 1;
  }

  inspector_set_verbosity(verbosity_level);

  zx_handle_t thread_self = thrd_get_zx_handle(thrd_current());
  if (thread_self == ZX_HANDLE_INVALID) {
    print_error("unable to get thread self");
    return 1;
  }

  zx_handle_t process;
  zx_obj_type_t type;
  status = get_task_by_koid(pid, &type, &process);
  if (status < 0) {
    print_zx_error(status, "unable to get a handle to %" PRIu64, pid);
    return 1;
  }

  if (type != ZX_OBJ_TYPE_PROCESS) {
    print_error("PID %" PRIu64 " is not a process. Threads can only be dumped from processes", pid);
    return 1;
  }

  char process_name[ZX_MAX_NAME_LEN];
  status = zx_object_get_property(process, ZX_PROP_NAME, process_name, sizeof(process_name));
  if (status < 0) {
    strlcpy(process_name, "unknown", sizeof(process_name));
  }

  printf("Backtrace of threads of process %" PRIu64 ": %s\n", pid, process_name);

  dump_all_threads(pid, process, verbosity_level);
  zx_handle_close(process);

  return 0;
}
