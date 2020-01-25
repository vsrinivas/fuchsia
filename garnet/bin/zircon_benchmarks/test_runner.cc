// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <perftest/perftest.h>

#include "round_trips.h"

int main(int argc, char** argv) {
  // Check for the argument used by test cases for launching subprocesses.
  if (argc == 3 && strcmp(argv[1], "--subprocess") == 0) {
    RunSubprocess(argv[2]);
    return 0;
  }

  return perftest::PerfTestMain(argc, argv, "fuchsia.microbenchmarks");
}
