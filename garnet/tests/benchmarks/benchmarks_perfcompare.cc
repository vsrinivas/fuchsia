// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This target is used for the performance comparison (perfcompare) CQ bots,
// which compare performance before and after a change.
//
// This target runs a subset of benchmarks for the Garnet layer.  It runs a
// subset of what benchmarks.cc runs.  The reason for running a subset is that
// the full set of tests currently takes too long and tends to exceed the bot
// timeout.

#include "garnet/testing/benchmarking/benchmarking.h"
#include "src/lib/fxl/strings/string_printf.h"

int main(int argc, const char** argv) {
  auto maybe_benchmarks_runner =
      benchmarking::BenchmarksRunner::Create(argc, argv);
  if (!maybe_benchmarks_runner) {
    exit(1);
  }

  auto& benchmarks_runner = *maybe_benchmarks_runner;

  // Reduce the number of iterations of each perf test within each process
  // given that we are launching each process multiple times.
  std::vector<std::string> extra_args = {"--runs", "100"};

  // Run these processes multiple times in order to account for
  // between-process variation in results (e.g. due to memory layout chosen
  // when a process starts).
  for (int process = 0; process < 30; ++process) {
    // Performance tests implemented in the Zircon repo.
    benchmarks_runner.AddLibPerfTestBenchmark(
        fxl::StringPrintf("zircon.perf_test_process%06d", process),
        "/pkgfs/packages/garnet_benchmarks/0/test/sys/perf-test",
        extra_args);

    // Performance tests implemented in the Garnet repo (the name
    // "zircon_benchmarks" is now misleading).
    benchmarks_runner.AddLibPerfTestBenchmark(
        fxl::StringPrintf("zircon_benchmarks_process%06d", process),
        "/pkgfs/packages/zircon_benchmarks/0/test/zircon_benchmarks",
        extra_args);
  }

  benchmarks_runner.Finish();
}
