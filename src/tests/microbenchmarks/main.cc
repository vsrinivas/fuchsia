// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "main.h"

#include <perftest/perftest.h>

#include "round_trips.h"

// The zeroth command line argument, argv[0], used for locating this process's
// executable in order to find dependencies.
const char* argv0;

int main(int argc, char** argv) {
  argv0 = argc >= 1 ? argv[0] : "";

#if defined(__Fuchsia__)
  // Check for the argument used by test cases for launching subprocesses.
  if (argc == 4 && strcmp(argv[1], "--subprocess") == 0) {
    RunSubprocess(argv[2], argv[3]);
    return 0;
  }
#endif

  const char* test_suite = "fuchsia.microbenchmarks";
  const char* env_test_suite = std::getenv("TEST_SUITE_LABEL");
  if (env_test_suite) {
    test_suite = env_test_suite;
  }

  return perftest::PerfTestMain(argc, argv, test_suite);
}
