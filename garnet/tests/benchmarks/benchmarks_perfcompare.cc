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

int main(int argc, const char** argv) {
  auto maybe_benchmarks_runner =
      benchmarking::BenchmarksRunner::Create(argc, argv);
  if (!maybe_benchmarks_runner) {
    exit(1);
  }

  auto& benchmarks_runner = *maybe_benchmarks_runner;

  // Performance tests implemented in the Zircon repo.
  benchmarks_runner.AddLibPerfTestBenchmark(
      "zircon.perf_test",
      "/pkgfs/packages/garnet_benchmarks/0/test/sys/perf-test");

  // Performance tests implemented in the Garnet repo (the name
  // "zircon_benchmarks" is now misleading).
  benchmarks_runner.AddLibPerfTestBenchmark(
      "zircon_benchmarks",
      "/pkgfs/packages/zircon_benchmarks/0/test/zircon_benchmarks");

  benchmarks_runner.Finish();
}
