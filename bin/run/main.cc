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

int main(int argc, const char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: run <program> <args>*\n");
    return 1;
  }
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = argv[1];
  for (int i = 0; i < argc - 2; ++i) {
    launch_info.arguments.push_back(argv[2 + i]);
  }

  launch_info.out = CloneFileDescriptor(STDOUT_FILENO);
  launch_info.err = CloneFileDescriptor(STDERR_FILENO);

  // Connect to the Launcher service through our static environment.
  fuchsia::sys::LauncherSyncPtr launcher;
  fuchsia::sys::ConnectToEnvironmentService(launcher.NewRequest());

  fuchsia::sys::ComponentControllerSyncPtr controller;
  launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

  int64_t return_code;
  if (!controller->Wait(&return_code)) {
    fprintf(stderr, "%s exited without a return code\n", argv[1]);
    return 1;
  }
  zx_process_exit(return_code);
  return 0;
}
