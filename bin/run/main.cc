// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/limits.h>
#include <lib/fdio/util.h>
#include <stdio.h>

#include <fuchsia/sys/cpp/fidl.h>
#include <zircon/syscalls.h>

#include "lib/app/cpp/environment_services.h"

static fuchsia::sys::FileDescriptorPtr CloneFileDescriptor(int fd) {
  zx_handle_t handles[FDIO_MAX_HANDLES] = {0, 0, 0};
  uint32_t types[FDIO_MAX_HANDLES] = {
      ZX_HANDLE_INVALID,
      ZX_HANDLE_INVALID,
      ZX_HANDLE_INVALID,
  };
  zx_status_t status = fdio_clone_fd(fd, 0, handles, types);
  if (status <= 0)
    return nullptr;
  fuchsia::sys::FileDescriptorPtr result = fuchsia::sys::FileDescriptor::New();
  result->type0 = types[0];
  result->handle0 = zx::handle(handles[0]);
  result->type1 = types[1];
  result->handle1 = zx::handle(handles[1]);
  result->type2 = types[2];
  result->handle2 = zx::handle(handles[2]);
  return result;
}

static void consume_arg(int* argc, const char*** argv) {
  --(*argc);
  ++(*argv);
}

int main(int argc, const char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: run [-d] <program> <args>*\n");
    return 1;
  }
  // argv[0] is the program name;
  consume_arg(&argc, &argv);

  bool daemonize = false;
  if (std::string(argv[0]) == "-d") {
    daemonize = true;
    consume_arg(&argc, &argv);
  }
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = argv[0];
  std::string program_name = argv[0];
  consume_arg(&argc, &argv);
  while (argc) {
    launch_info.arguments.push_back(*argv);
    consume_arg(&argc, &argv);
  }

  // Connect to the Launcher service through our static environment.
  fuchsia::sys::LauncherSync2Ptr launcher;
  fuchsia::sys::ConnectToEnvironmentService(launcher.NewRequest());

  if (daemonize) {
    launcher->CreateComponent(std::move(launch_info), {});
    return 0;
  }

  launch_info.out = CloneFileDescriptor(STDOUT_FILENO);
  launch_info.err = CloneFileDescriptor(STDERR_FILENO);
  fuchsia::sys::ComponentControllerSync2Ptr controller;
  launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

  int64_t return_code;
  if (controller->Wait(&return_code).statvs != ZX_OK) {
    fprintf(stderr, "%s exited without a return code\n", program_name.c_str());
    return 1;
  }
  zx_process_exit(return_code);
  return 0;
}
