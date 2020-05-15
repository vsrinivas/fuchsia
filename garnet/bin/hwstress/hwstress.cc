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

namespace hwstress {

// Convert double representing a number of seconds to a zx::duration.
zx::duration SecsToDuration(double secs) {
  return zx::nsec(static_cast<int64_t>(secs * 1'000'000'000.0));
}

// Convert zx::duration to double representing the number of seconds.
double DurationToSecs(zx::duration d) { return d.to_nsecs() / 1'000'000'000.0; }

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
  zx::time finish_time = zx::deadline_after(duration);

  // Print start banner.
  if (finish_time == zx::time::infinite()) {
    printf("Exercising CPU until stopped...\n");
  } else {
    printf("Exercising CPU for %0.2f seconds...\n", DurationToSecs(duration));
  }

  // Minimal viable product.
  while (zx::clock::get_monotonic() < finish_time) {
  }

  printf("Done.\n");
  return 0;
}

}  // namespace hwstress
