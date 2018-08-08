// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>

#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "garnet/bin/chrealm/chrealm.h"
#include "lib/fxl/command_line.h"

static void PrintUsage() {
  fprintf(stderr,
          "Usage: chrealm <realm> [-- cmd arg1 ... argn]\n"
          "Starts a process with the services of the given realm, as a path "
          "under /hub.\n"
          "The process has the same namespace as the calling process, but "
          "with /svc replaced with the /svc of <realm>.\n"
          "The process will run [cmd arg1 ... argn] if specified, otherwise "
          "it defaults to /boot/bin/sh.\n"
          "Examples:\n"
          "chrealm /hub/r/myrealm/<koid> (assume services of myrealm)\n"
          "chrealm /hub/r/myrealm/<koid> -- /system/bin/ls /svc "
          "(list services of myrealm from inside myrealm)\n");
}

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  const auto& positional_args = command_line.positional_args();
  if (positional_args.size() < 1 || positional_args.size() == 2) {
    PrintUsage();
    return 1;
  }
  if (positional_args.size() >= 2 && positional_args[1] != "--") {
    PrintUsage();
    return 1;
  }

  const char* kDefaultArgv[] = {"/boot/bin/sh", nullptr};
  const char** child_argv = kDefaultArgv;
  if (argc > 2) {
    child_argv = &argv[3];
  }

  int64_t code;
  zx_status_t status =
      chrealm::RunBinaryInRealm(positional_args[0], child_argv, &code);
  if (status != ZX_OK) {
    fprintf(stderr, "chrealm failed: %s\n", zx_status_get_string(status));
    return 1;
  }
  zx_process_exit(code);
  return 0;
}
