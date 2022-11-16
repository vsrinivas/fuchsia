// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/limits.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/termination_reason.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <utility>

#include "src/lib/fxl/strings/string_printf.h"

using fuchsia::sys::TerminationReason;
using fxl::StringPrintf;

static void consume_arg(int* argc, const char*** argv) {
  --(*argc);
  ++(*argv);
}

static void CheckUrl(const std::string& url) {
  if (url.rfind(".cmx") == url.size() - 4) {
    // Valid URL.
  } else if (url.rfind(".cm") == url.size() - 3) {
    fprintf(stderr,
            "\"%s\" is a Components v2 URL. `run` only supports v1 "
            "components. See: "
            "https://fuchsia.dev/fuchsia-src/development/components/run\n",
            url.c_str());
    zx_process_exit(1);
  } else {
    fprintf(stderr,
            "\"%s\" is not a valid component URL. Component URLs must "
            "end in `.cmx`.\n",
            url.c_str());
    zx_process_exit(1);
  }
}

void launch(fuchsia::sys::LauncherSyncPtr launcher, fuchsia::sys::ComponentControllerPtr controller,
            fuchsia::sys::LaunchInfo launch_info, async::Loop* loop, bool daemonize) {
  if (daemonize) {
    launcher->CreateComponent(std::move(launch_info), {});
    zx_process_exit(0);
  }

  std::string url = launch_info.url;
  launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

  controller.events().OnTerminated = [&url](int64_t return_code,
                                            TerminationReason termination_reason) {
    if (termination_reason != TerminationReason::EXITED) {
      fprintf(stderr, "%s: %s\n", url.c_str(),
              sys::HumanReadableTerminationReason(termination_reason).c_str());
    }
    zx_process_exit(return_code);
  };
  loop->Run();
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

  std::string program_name = argv[0];
  consume_arg(&argc, &argv);

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = program_name;
  launch_info.out = sys::CloneFileDescriptor(STDOUT_FILENO);
  launch_info.err = sys::CloneFileDescriptor(STDERR_FILENO);
  launch_info.arguments.emplace();
  while (argc) {
    launch_info.arguments->push_back(*argv);
    consume_arg(&argc, &argv);
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto services = sys::ServiceDirectory::CreateFromNamespace();

  // Connect to the Launcher service through our static environment.
  fuchsia::sys::LauncherSyncPtr launcher;
  services->Connect(launcher.NewRequest());
  fuchsia::sys::ComponentControllerPtr controller;

  CheckUrl(launch_info.url);
  launch(std::move(launcher), std::move(controller), std::move(launch_info), &loop, daemonize);

  return 0;
}
