// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include <task-utils/get.h>

#include "handles-internal.h"

namespace {

zx_status_t get_handles(zx_handle_t process, std::vector<zx_info_handle_extended_t>* out) {
  size_t avail = 32;

  while (true) {
    out->resize(avail);
    auto size = avail * sizeof(zx_info_handle_extended_t);
    size_t actual = 0;
    auto status =
        zx_object_get_info(process, ZX_INFO_HANDLE_TABLE, out->data(), size, &actual, &avail);
    if (status != ZX_OK) {
      return status;
    }
    if (actual < avail) {
      avail += 8u;
      continue;
    }
    out->resize(actual);
    return ZX_OK;
  }
}

void print_help(FILE* f) {
  fprintf(f, "Usage: handles [options] <pid>\n");
  fprintf(f, "  Prints the handle table of a process.\n");
  fprintf(f, "Options:\n");
  fprintf(f, " -t|--task     Only include process|thread|job in the output\n");
  fprintf(f, " -v|--vmo      Only include vmos in the output\n");
  fprintf(f, " -p|--port     Only include ports in the output\n");
  fprintf(f, " -c|--channel  Only include threads in the output\n");
  fprintf(f, " -e|--event    Only include events | eventpairs in the output\n");
  fprintf(f, " -s|--socket   Only include sockets in the output\n");
  fprintf(f, " -r|--reverse  Exclude objects specified in the filter\n");
  fprintf(f, " -h|--help     Display this message\n");
}

}  // namespace

int main(int argc, char** argv) {
  Filter filter = kAll;
  bool reverse_filter = false;

  while (true) {
    static option options[] = {
        {"task", no_argument, nullptr, 't'},
        {"vmo", no_argument, nullptr, 'v'},
        {"port", no_argument, nullptr, 'p'},
        {"channel", no_argument, nullptr, 'c'},
        {"event", no_argument, nullptr, 'e'},
        {"socket", no_argument, nullptr, 's'},
        {"reverse", no_argument, nullptr, 'r'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };

    int option_index = 0;
    int c = getopt_long(argc, argv, "tvpcesrh", options, &option_index);
    if (c < 0) {
      break;
    }
    switch (c) {
      case 't':
        filter += kThread;
        filter += kProcess;
        filter += kJob;
        break;
      case 'v':
        filter += kVmo;
        break;
      case 'p':
        filter += kPort;
        break;
      case 'c':
        filter += kChannel;
        break;
      case 'e':
        filter += kEvent;
        filter += kEventPair;
        break;
      case 's':
        filter += kSocket;
        break;
      case 'r':
        reverse_filter = true;
        break;
      case 'h':
      default:
        print_help(c == 'h' ? stdout : stderr);
        return c == 'h' ? 0 : 1;
    }
  }  // while

  if ((filter != kAll) && reverse_filter) {
    filter = ~filter;
  }

  if (argc == 1) {
    print_help(stderr);
    return 1;
  }

  zx_koid_t koid = 0;

  if (optind < argc) {
    char* end;
    koid = strtoull(argv[optind], &end, 0);
    if (koid == 0) {
      fprintf(stderr, "handles: unrecognized extra arguments:");
      while (optind < argc) {
        fprintf(stderr, " %s", argv[optind++]);
      }
      fprintf(stderr, "\n");
      print_help(stderr);
      return 1;
    }
  }

  zx_handle_t process;
  zx_obj_type_t type;
  auto status = get_task_by_koid(koid, &type, &process);
  if (status != ZX_OK) {
    fprintf(stderr, "handles: can't get process, error %d\n", status);
    return 1;
  }

  if (type != ZX_OBJ_TYPE_PROCESS) {
    zx_handle_close(process);
    fprintf(stderr, "handles: koid %lu is not a process id\n", koid);
    return 1;
  }

  std::vector<zx_info_handle_extended_t> handles;
  status = get_handles(process, &handles);
  if (status != ZX_OK) {
    fprintf(stderr, "handles: syscall error %d\n", status);
    return 1;
  }

  print_handles(stdout, handles, filter);
  return 0;
}
