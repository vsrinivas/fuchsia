// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/tracing/kernel/cpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/status.h>

#include <fbl/string.h>
#include <fbl/unique_fd.h>

namespace {
constexpr char kKtraceControllerSvc[] = "/svc/fuchsia.tracing.kernel.Controller";
constexpr char kKtraceReaderSvc[] = "/svc/fuchsia.tracing.kernel.Reader";

constexpr char kUsage[] =
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

void PrintUsage(FILE* f) { fputs(kUsage, f); }

zx::channel OpenKTraceReader() {
  int fd{open(kKtraceReaderSvc, O_RDWR)};
  if (fd < 0) {
    fprintf(stderr, "Cannot open trace reader %s: %s\n", kKtraceReaderSvc, strerror(errno));
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

zx::channel OpenKtraceController() {
  int fd{open(kKtraceControllerSvc, O_RDWR)};
  if (fd < 0) {
    fprintf(stderr, "Cannot open trace controller %s: %s\n", kKtraceControllerSvc, strerror(errno));
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

int LogFidlError(zx_status_t status) {
  fprintf(stderr, "Error in FIDL request: %s(%d)\n", zx_status_get_string(status), status);
  return EXIT_FAILURE;
}

int DoStart(uint32_t group_mask) {
  fuchsia::tracing::kernel::ControllerSyncPtr controller;
  controller.Bind(OpenKtraceController());
  zx_status_t start_status;
  zx_status_t status = controller->Start(group_mask, &start_status);
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

int DoStop() {
  fuchsia::tracing::kernel::ControllerSyncPtr controller;
  controller.Bind(OpenKtraceController());
  zx_status_t stop_status;
  zx_status_t status = controller->Stop(&stop_status);
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

int DoRewind() {
  fuchsia::tracing::kernel::ControllerSyncPtr controller;
  controller.Bind(OpenKtraceController());
  zx_status_t rewind_status;
  zx_status_t status = controller->Rewind(&rewind_status);
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

int DoWritten() {
  fuchsia::tracing::kernel::ReaderSyncPtr reader;
  reader.Bind(OpenKTraceReader());
  zx_status_t written_status;
  uint64_t bytes_written;
  zx_status_t status = reader->GetBytesWritten(&written_status, &bytes_written);
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

int DoSave(const char* path) {
  fuchsia::tracing::kernel::ReaderSyncPtr reader;
  reader.Bind(OpenKTraceReader());
  fbl::unique_fd out_fd(open(path, O_CREAT | O_TRUNC | O_WRONLY, 0666));
  if (!out_fd.is_valid()) {
    fprintf(stderr, "Unable to open file for writing: %s, %s\n", path, strerror(errno));
    return EXIT_FAILURE;
  }

  // Read/write this many bytes at a time.
  std::vector<uint8_t> buf;
  size_t read_size = 4096;
  size_t offset = 0;
  zx_status_t out_status;
  zx_status_t status;
  while ((status = reader->ReadAt(read_size, offset, &out_status, &buf)) == ZX_OK &&
         out_status == ZX_OK) {
    if (buf.size() == 0) {
      break;
    }
    offset += buf.size();
    size_t bytes_written = write(out_fd.get(), buf.data(), buf.size());
    if (bytes_written < 0) {
      fprintf(stderr, "I/O error saving buffer: %s\n", strerror(errno));
      return EXIT_FAILURE;
    }
    if (bytes_written != buf.size()) {
      fprintf(stderr, "Short write saving buffer: %zd vs %zd\n", bytes_written, buf.size());
      return EXIT_FAILURE;
    }
  }

  return EXIT_SUCCESS;
}

void EnsureNArgs(const fbl::String& cmd, int argc, int expected_argc) {
  if (argc != expected_argc) {
    fprintf(stderr, "Unexpected number of args for command %s\n", cmd.c_str());
    PrintUsage(stderr);
    exit(EXIT_FAILURE);
  }
}
}  // namespace

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
