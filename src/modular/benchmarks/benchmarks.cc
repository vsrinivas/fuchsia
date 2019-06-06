// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This target runs modular benchmarks.

#include "garnet/testing/benchmarking/benchmarking.h"

int main(int argc, const char** argv) {
  auto maybe_benchmarks_runner =
      benchmarking::BenchmarksRunner::Create(argc, argv);
  if (!maybe_benchmarks_runner) {
    exit(1);
  }

  auto& benchmarks_runner = *maybe_benchmarks_runner;

  benchmarks_runner.AddTspecBenchmark("story_benchmark_test",
                                      "/pkgfs/packages/modular_benchmarks/0/"
                                      "data/story_benchmark_test.tspec");

  benchmarks_runner.Finish();
}
