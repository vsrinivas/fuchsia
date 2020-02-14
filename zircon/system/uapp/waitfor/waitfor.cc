// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/block/partition/c/fidl.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// for guid printing
#include <zircon/device/block.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>

#include <gpt/c/gpt.h>

void usage(void) {
  fprintf(stderr,
          "usage: waitfor <expr>+        wait for devices to be published\n"
          "\n"
          "expr:  class=<name>           device class <name>   (required)\n"
          "\n"
          "       topo=<path>            topological path starts with <path>\n"
          "       part.guid=<guid>       block device GUID matches <guid>\n"
          "       part.type.guid=<guid>  partition type GUID matches <guid>\n"
          "       part.name=<name>       partition name matches <name>\n"
          "\n"
          "       timeout=<msec>         fail if no match after <msec> milliseconds\n"
          "       print                  write name of matching devices to stdout\n"
          "       forever                don't stop after the first match\n"
          "                              also don't fail on timeout after first match\n"
          "       verbose                print debug chatter to stderr\n"
          "\n"
          "example: waitfor class=block part.name=system print\n");
}

static bool verbose = false;
static bool print = false;
static bool forever = false;
static bool matched = false;
static zx_duration_t timeout = 0;
const char* devclass = NULL;

typedef struct rule {
  zx_status_t (*func)(const char* arg, int fd);
  const char* arg;
  list_node_t node;
} rule_t;

static list_node_t rules = LIST_INITIAL_VALUE(rules);

zx_status_t watchcb(int dirfd, int event, const char* fn, void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }
  if (verbose) {
    fprintf(stderr, "waitfor: device='/dev/class/%s/%s'\n", devclass, fn);
  }
  int fd;
  if ((fd = openat(dirfd, fn, O_RDONLY)) < 0) {
    fprintf(stderr, "waitfor: warning: failed to open '/dev/class/%s/%s'\n", devclass, fn);
    return ZX_OK;
  }

  rule_t* r;
  list_for_every_entry (&rules, r, rule_t, node) {
    zx_status_t status = r->func(r->arg, fd);
    switch (status) {
      case ZX_OK:
        // rule matched
        continue;
      case ZX_ERR_NEXT:
        // rule did not match
        close(fd);
        return ZX_OK;
      default:
        // fatal error
        close(fd);
        return status;
    }
  }

  matched = true;
  close(fd);

  if (print) {
    printf("/dev/class/%s/%s\n", devclass, fn);
  }

  if (forever) {
    return ZX_OK;
  } else {
    return ZX_ERR_STOP;
  }
}

// Expression evaluators return OK on match, NEXT on no-match
// any other error is fatal

zx_status_t expr_topo(const char* arg, int fd) {
  char topo[1024 + 1];

  fdio_t* io = fdio_unsafe_fd_to_io(fd);
  if (io == NULL) {
    return ZX_ERR_NEXT;
  }
  zx_status_t call_status = ZX_OK;
  size_t path_len;
  auto resp = ::llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(
      zx::unowned_channel(fdio_unsafe_borrow_channel(io)));
  zx_status_t status = resp.status();

  if (resp->result.is_err()) {
    call_status = resp->result.err();
  } else {
    path_len = resp->result.response().path.size();
    auto r = resp->result.response();
    memcpy(topo, r.path.data(), r.path.size());
  }
  fdio_unsafe_release(io);
  if (status != ZX_OK || call_status != ZX_OK) {
    fprintf(stderr, "waitfor: warning: cannot read topo path\n");
    return ZX_ERR_NEXT;
  }
  topo[path_len] = 0;

  if (verbose) {
    fprintf(stderr, "waitfor: topo='%s'\n", topo);
  }
  size_t len = strlen(arg);
  if ((path_len < len) || strncmp(arg, topo, len)) {
    return ZX_ERR_NEXT;
  } else {
    return ZX_OK;
  }
}

zx_status_t expr_part_guid(const char* arg, int fd) {
  fdio_t* io = fdio_unsafe_fd_to_io(fd);
  if (io == NULL) {
    return ZX_ERR_NEXT;
  }
  fuchsia_hardware_block_partition_GUID guid;
  zx_status_t status;
  zx_status_t io_status = fuchsia_hardware_block_partition_PartitionGetInstanceGuid(
      fdio_unsafe_borrow_channel(io), &status, &guid);
  fdio_unsafe_release(io);
  if (io_status != ZX_OK || status != ZX_OK) {
    fprintf(stderr, "waitfor: warning: cannot read partition guid\n");
    return ZX_ERR_NEXT;
  }
  char text[GPT_GUID_STRLEN];
  uint8_to_guid_string(text, guid.value);
  if (verbose) {
    fprintf(stderr, "waitfor: part.guid='%s'\n", text);
  }
  if (strcasecmp(text, arg)) {
    return ZX_ERR_NEXT;
  } else {
    return ZX_OK;
  }
}

zx_status_t expr_part_type_guid(const char* arg, int fd) {
  fdio_t* io = fdio_unsafe_fd_to_io(fd);
  if (io == NULL) {
    return ZX_ERR_NEXT;
  }
  fuchsia_hardware_block_partition_GUID guid;
  zx_status_t status;
  zx_status_t io_status = fuchsia_hardware_block_partition_PartitionGetTypeGuid(
      fdio_unsafe_borrow_channel(io), &status, &guid);
  fdio_unsafe_release(io);
  if (io_status != ZX_OK || status != ZX_OK) {
    fprintf(stderr, "waitfor: warning: cannot read type guid\n");
    return ZX_ERR_NEXT;
  }
  char text[GPT_GUID_STRLEN];
  uint8_to_guid_string(text, guid.value);
  if (verbose) {
    fprintf(stderr, "waitfor: part.type.guid='%s'\n", text);
  }
  if (strcasecmp(text, arg)) {
    return ZX_ERR_NEXT;
  } else {
    return ZX_OK;
  }
}

zx_status_t expr_part_name(const char* arg, int fd) {
  char name[NAME_MAX + 1];

  fdio_t* io = fdio_unsafe_fd_to_io(fd);
  if (io == NULL) {
    return ZX_ERR_NEXT;
  }
  zx_status_t status;
  size_t actual;
  zx_status_t io_status = fuchsia_hardware_block_partition_PartitionGetName(
      fdio_unsafe_borrow_channel(io), &status, name, sizeof(name), &actual);
  fdio_unsafe_release(io);
  if (io_status != ZX_OK || status != ZX_OK) {
    fprintf(stderr, "waitfor: warning: cannot read partition name\n");
    return ZX_ERR_NEXT;
  }
  if (verbose) {
    fprintf(stderr, "waitfor: part.name='%s'\n", name);
  }
  if (strcmp(arg, name)) {
    return ZX_ERR_NEXT;
  } else {
    return ZX_OK;
  }
}

void new_rule(const char* arg, zx_status_t (*func)(const char* arg, int fd)) {
  rule_t* r = (rule_t*)malloc(sizeof(rule_t));
  if (r == NULL) {
    fprintf(stderr, "waitfor: error: out of memory\n");
    exit(1);
  }
  r->func = func;
  r->arg = arg;
  list_add_tail(&rules, &r->node);
}

int main(int argc, char** argv) {
  int dirfd = -1;

  if (argc == 1) {
    usage();
    exit(1);
  }

  while (argc > 1) {
    if (!strcmp(argv[1], "print")) {
      print = true;
    } else if (!strcmp(argv[1], "verbose")) {
      verbose = true;
    } else if (!strcmp(argv[1], "forever")) {
      forever = true;
    } else if (!strncmp(argv[1], "timeout=", 8)) {
      timeout = ZX_MSEC(atoi(argv[1] + 8));
      if (timeout == 0) {
        fprintf(stderr, "waitfor: error: timeout of 0 not allowed\n");
        exit(1);
      }
    } else if (!strncmp(argv[1], "class=", 6)) {
      devclass = argv[1] + 6;
    } else if (!strncmp(argv[1], "topo=", 5)) {
      new_rule(argv[1] + 5, expr_topo);
    } else if (!strncmp(argv[1], "part.guid=", 10)) {
      new_rule(argv[1] + 10, expr_part_guid);
    } else if (!strncmp(argv[1], "part.type.guid=", 15)) {
      new_rule(argv[1] + 15, expr_part_guid);
    } else if (!strncmp(argv[1], "part.name=", 10)) {
      new_rule(argv[1] + 10, expr_part_name);
    } else {
      fprintf(stderr, "waitfor: error: unknown expr '%s'\n\n", argv[1]);
      usage();
      exit(1);
    }
    argc--;
    argv++;
  }

  if (devclass == NULL) {
    fprintf(stderr, "waitfor: error: no class specified\n");
    exit(1);
  }

  if (list_is_empty(&rules)) {
    fprintf(stderr, "waitfor: error: no match expressions specified\n");
    exit(1);
  }

  char path[strlen(devclass) + strlen("/dev/class/") + 1];
  sprintf(path, "/dev/class/%s", devclass);

  if ((dirfd = open(path, O_DIRECTORY | O_RDONLY)) < 0) {
    fprintf(stderr, "waitfor: error: cannot watch class '%s'\n", devclass);
    exit(1);
  }

  zx_time_t deadline;
  if (timeout == 0) {
    deadline = ZX_TIME_INFINITE;
  } else {
    deadline = zx_deadline_after(timeout);
  }
  zx_status_t status = fdio_watch_directory(dirfd, watchcb, deadline, NULL);
  close(dirfd);

  switch (status) {
    case ZX_ERR_STOP:
      // clean exit on a match
      return 0;
    case ZX_ERR_TIMED_OUT:
      // timeout, but if we're in forever mode and matched any, its good
      if (matched && forever) {
        return 0;
      }
      break;
    default:
      // any other situation? failure
      return 1;
  }
}
