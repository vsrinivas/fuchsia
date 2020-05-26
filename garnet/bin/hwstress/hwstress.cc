// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwstress.h"

#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <stdio.h>

#include <string>
#include <utility>

#include "args.h"
#include "cpu.h"
#include "util.h"

namespace hwstress {

int Run(int argc, const char** argv) {
  // Parse arguments.
  fitx::result<std::string, CommandLineArgs> result =
      ParseArgs(fbl::Span<const char* const>(argv, argc));
  if (result.is_error()) {
    fprintf(stderr, "Error: %s\n\n", result.error_value().c_str());
    PrintUsage();
    return 1;
  }
  const CommandLineArgs& args = result.value();

  // Print help and exit if requested.
  if (args.help) {
    PrintUsage();
    return 0;
  }

  // Calculate finish time.
  zx::duration duration = args.test_duration_seconds == 0
                              ? zx::duration::infinite()
                              : SecsToDuration(args.test_duration_seconds);

  // Run the CPU tests.
  StressCpu(duration);

  return 0;
}

}  // namespace hwstress
