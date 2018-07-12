// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <gflags/gflags.h>
#include <perftest/perftest.h>

#include "channels.h"
#include "round_trips.h"

// Command line argument used internally for launching subprocesses.
DEFINE_string(subprocess, "", "Launch a process to run the named function");

namespace fbenchmark {

int BenchmarksMain(int argc, char** argv, bool run_gbenchmark) {
  // Check for the argument used by test cases for launching subprocesses.
  // We check for it before calling gflags because gflags gives an error for
  // any options it does not recognize, such as those accepted by
  // perftest::PerfTestMain().
  if (argc >= 2 && strcmp(argv[1], "--subprocess") == 0) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    RunSubprocess(FLAGS_subprocess.c_str());
    return 0;
  }

  static const char kTestSuiteName[] = "fuchsia.zircon_benchmarks";

  if (argc >= 2 && strcmp(argv[1], "-p") == 0) {
    return perftest::PerfTestMain(argc, argv, kTestSuiteName);
  }
  if (run_gbenchmark) {
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    return 0;
  }
  return perftest::PerfTestMain(argc, argv, kTestSuiteName);
}

}  // namespace fbenchmark
