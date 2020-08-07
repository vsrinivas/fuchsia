// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/tracing/kernel/c/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/status.h>

#include <fbl/string.h>
#include <fbl/unique_fd.h>

static const char kDevicePath[] = "/dev/misc/ktrace";

static const char kUsage[] =
    "\
Usage: ktrace [options] <control>\n\
Where <control> is one of:\n\
  start <group_mask>  - start tracing\n\
  stop                - stop tracing\n\
  rewind              - rewind trace buffer\n\
  written             - print bytes written to trace buffer\n\
    Note: This value doesn't reset on \"rewind\". Instead, the rewind\n\
    takes effect on the next \"start\".\n\
  save <path>         - save contents of trace buffer to <path>\n\
\n\
Options:\n\
  --help  - Duh.\n\
";

static void PrintUsage(FILE* f) { fputs(kUsage, f); }

static fbl::unique_fd OpenKtraceDeviceAsFd() {
  fbl::unique_fd fd{open(kDevicePath, O_RDWR)};
  if (!fd.is_valid()) {
    fprintf(stderr, "Cannot open trace device %s: %s\n", kDevicePath, strerror(errno));
    exit(EXIT_FAILURE);
  }
  return fd;
}

static zx::channel OpenKtraceDeviceAsChannel() {
  int fd{open(kDevicePath, O_RDWR)};
  if (fd < 0) {
    fprintf(stderr, "Cannot open trace device %s: %s\n", kDevicePath, strerror(errno));
    exit(EXIT_FAILURE);
  }
  zx::channel channel;
  zx_status_t status = fdio_get_service_handle(fd, channel.reset_and_get_address());
  if (status != ZX_OK) {
    fprintf(stderr, "Unable to obtain channel handle from file descriptor: %s\n",
            zx_status_get_string(status));
    exit(EXIT_FAILURE);
  }
  return channel;
}

static int LogFidlError(zx_status_t status) {
  fprintf(stderr, "Error in FIDL request: %s(%d)\n", zx_status_get_string(status), status);
  return EXIT_FAILURE;
}

static int DoStart(uint32_t group_mask) {
  zx::channel channel{OpenKtraceDeviceAsChannel()};
  zx_status_t start_status;
  zx_status_t status =
      fuchsia_tracing_kernel_ControllerStart(channel.get(), group_mask, &start_status);
  if (status != ZX_OK) {
    return LogFidlError(status);
  }
  if (start_status != ZX_OK) {
    fprintf(stderr, "Error starting ktrace: %s(%d)\n", zx_status_get_string(start_status),
            start_status);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

static int DoStop() {
  zx::channel channel{OpenKtraceDeviceAsChannel()};
  zx_status_t stop_status;
  zx_status_t status = fuchsia_tracing_kernel_ControllerStop(channel.get(), &stop_status);
  if (status != ZX_OK) {
    return LogFidlError(status);
  }
  if (stop_status != ZX_OK) {
    fprintf(stderr, "Error stopping ktrace: %s(%d)\n", zx_status_get_string(stop_status),
            stop_status);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

static int DoRewind() {
  zx::channel channel{OpenKtraceDeviceAsChannel()};
  zx_status_t rewind_status;
  zx_status_t status = fuchsia_tracing_kernel_ControllerRewind(channel.get(), &rewind_status);
  if (status != ZX_OK) {
    return LogFidlError(status);
  }
  if (rewind_status != ZX_OK) {
    fprintf(stderr, "Error rewinding ktrace: %s(%d)\n", zx_status_get_string(rewind_status),
            rewind_status);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

static int DoWritten() {
  zx::channel channel{OpenKtraceDeviceAsChannel()};
  zx_status_t written_status;
  uint64_t bytes_written;
  zx_status_t status = fuchsia_tracing_kernel_ControllerGetBytesWritten(
      channel.get(), &written_status, &bytes_written);
  if (status != ZX_OK) {
    return LogFidlError(status);
  }
  if (written_status != ZX_OK) {
    fprintf(stderr, "Error getting bytes written: %s(%d)\n", zx_status_get_string(written_status),
            written_status);
    return EXIT_FAILURE;
  }
  printf("Bytes written: %ld\n", bytes_written);
  return EXIT_SUCCESS;
}

static int DoSave(const char* path) {
  fbl::unique_fd in_fd{OpenKtraceDeviceAsFd()};
  fbl::unique_fd out_fd(open(path, O_CREAT | O_TRUNC | O_WRONLY, 0666));
  if (!out_fd.is_valid()) {
    fprintf(stderr, "Unable to open file for writing: %s, %s\n", path, strerror(errno));
    return EXIT_FAILURE;
  }

  // Read/write this many bytes at a time.
  char buf[4096];
  ssize_t bytes_read;
  while ((bytes_read = read(in_fd.get(), buf, sizeof(buf))) > 0) {
    ssize_t bytes_written = write(out_fd.get(), buf, bytes_read);
    if (bytes_written < 0) {
      fprintf(stderr, "I/O error saving buffer: %s\n", strerror(errno));
      return EXIT_FAILURE;
    }
    if (bytes_written != bytes_read) {
      fprintf(stderr, "Short write saving buffer: %zd vs %zd\n", bytes_written, bytes_read);
      return EXIT_FAILURE;
    }
  }

  return EXIT_SUCCESS;
}

static void EnsureNArgs(const fbl::String& cmd, int argc, int expected_argc) {
  if (argc != expected_argc) {
    fprintf(stderr, "Unexpected number of args for command %s\n", cmd.c_str());
    PrintUsage(stderr);
    exit(EXIT_FAILURE);
  }
}

int main(int argc, char** argv) {
  if (argc >= 2 && strcmp(argv[1], "--help") == 0) {
    PrintUsage(stdout);
    return EXIT_SUCCESS;
  }

  if (argc < 2) {
    PrintUsage(stderr);
    return EXIT_FAILURE;
  }
  const fbl::String cmd{argv[1]};

  if (cmd == "start") {
    EnsureNArgs(cmd, argc, 3);
    int group_mask = atoi(argv[2]);
    if (group_mask < 0) {
      fprintf(stderr, "Invalid group mask\n");
      return EXIT_FAILURE;
    }
    return DoStart(group_mask);
  } else if (cmd == "stop") {
    EnsureNArgs(cmd, argc, 2);
    return DoStop();
  } else if (cmd == "rewind") {
    EnsureNArgs(cmd, argc, 2);
    return DoRewind();
  } else if (cmd == "written") {
    EnsureNArgs(cmd, argc, 2);
    return DoWritten();
  } else if (cmd == "save") {
    EnsureNArgs(cmd, argc, 3);
    const char* path = argv[2];
    return DoSave(path);
  }

  PrintUsage(stderr);
  return EXIT_FAILURE;
}
