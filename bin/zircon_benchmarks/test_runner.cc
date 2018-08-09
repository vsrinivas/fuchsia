// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gflags/gflags.h>
#include <perftest/perftest.h>

#include "round_trips.h"

// Command line argument used internally for launching subprocesses.
DEFINE_string(subprocess, "", "Launch a process to run the named function");

int main(int argc, char** argv) {
  // Check for the argument used by test cases for launching subprocesses.
  // We check for it before calling gflags because gflags gives an error for
  // any options it does not recognize, such as those accepted by
  // perftest::PerfTestMain().
  if (argc >= 2 && strcmp(argv[1], "--subprocess") == 0) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    RunSubprocess(FLAGS_subprocess.c_str());
    return 0;
  }

  return perftest::PerfTestMain(argc, argv, "fuchsia.zircon_benchmarks");
}
