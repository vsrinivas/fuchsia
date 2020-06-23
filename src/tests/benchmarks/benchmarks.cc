// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include "garnet/testing/benchmarking/benchmarking.h"

// This file no longer runs any interesting test cases, but it is left in
// place for testing (on the CQ) that garnet/testing/benchmarking/ works,
// because that is also used for running storage performance tests (which
// are not run on the CQ).
//
// TODO(fxb/51707): Remove this file once the storage performance tests are
// converted to SL4F.

namespace {

void AddPerfTests(benchmarking::BenchmarksRunner* benchmarks_runner, bool perfcompare_mode) {
  FX_DCHECK(benchmarks_runner);

  // Run one simple test case.
  std::vector<std::string> extra_args = {"--filter", "^Null$"};
  benchmarks_runner->AddLibPerfTestBenchmark("fuchsia_microbenchmarks",
                                             "/bin/fuchsia_microbenchmarks", extra_args);
}
}  // namespace

int main(int argc, const char** argv) {
  bool perfcompare_mode = false;
  if (argc >= 2 && strcmp(argv[1], "--perfcompare_mode") == 0) {
    perfcompare_mode = true;
    // Remove argv[1] from the argument list.
    for (int i = 2; i < argc; ++i)
      argv[i - 1] = argv[i];
    --argc;
  }

  auto maybe_benchmarks_runner = benchmarking::BenchmarksRunner::Create(argc, argv);
  if (!maybe_benchmarks_runner) {
    exit(1);
  }

  auto& benchmarks_runner = *maybe_benchmarks_runner;
  AddPerfTests(&benchmarks_runner, perfcompare_mode);

  benchmarks_runner.Finish();
}
