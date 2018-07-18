// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/termination_reason.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/util.h>
#include <lib/fxl/strings/string_printf.h>
#include <stdio.h>

#include <fuchsia/sys/cpp/fidl.h>
#include <zircon/syscalls.h>

#include "lib/component/cpp/environment_services.h"

using fuchsia::sys::TerminationReason;
using fxl::StringPrintf;

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
  fuchsia::sys::LauncherSyncPtr launcher;
  component::ConnectToEnvironmentService(launcher.NewRequest());

  if (daemonize) {
    launcher->CreateComponent(std::move(launch_info), {});
    return 0;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  launch_info.out = CloneFileDescriptor(STDOUT_FILENO);
  launch_info.err = CloneFileDescriptor(STDERR_FILENO);

  fuchsia::sys::ComponentControllerPtr controller;
  launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

  controller.events().OnTerminated = [&program_name](
                                         int64_t return_code,
                                         TerminationReason termination_reason) {
    if (termination_reason != TerminationReason::EXITED) {
      fprintf(stderr, "%s: %s\n", program_name.c_str(),
              component::HumanReadableTerminationReason(termination_reason)
                  .c_str());
    }
    zx_process_exit(return_code);
  };

  loop.Run();
  return 0;
}
