// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/fd.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/termination_reason.h>
#include <stdio.h>
#include <unistd.h>
#include <utility>
#include <zircon/syscalls.h>
#include <zircon/processargs.h>

using fuchsia::sys::TerminationReason;
using fxl::StringPrintf;

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
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto services = sys::ServiceDirectory::CreateFromNamespace();

  // Connect to the Launcher service through our static environment.
  fuchsia::sys::LauncherSyncPtr launcher;
  services->Connect(launcher.NewRequest());

  if (daemonize) {
    launcher->CreateComponent(std::move(launch_info), {});
    return 0;
  }

  launch_info.out = sys::CloneFileDescriptor(STDOUT_FILENO);
  launch_info.err = sys::CloneFileDescriptor(STDERR_FILENO);

  fuchsia::sys::ComponentControllerPtr controller;
  launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

  controller.events().OnTerminated = [&program_name](
                                         int64_t return_code,
                                         TerminationReason termination_reason) {
    if (termination_reason != TerminationReason::EXITED) {
      fprintf(stderr, "%s: %s\n", program_name.c_str(),
              sys::HumanReadableTerminationReason(termination_reason).c_str());
    }
    zx_process_exit(return_code);
  };

  loop.Run();
  return 0;
}
