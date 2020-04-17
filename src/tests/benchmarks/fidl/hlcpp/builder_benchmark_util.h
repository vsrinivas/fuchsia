// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_HLCPP_BUILDER_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_HLCPP_BUILDER_BENCHMARK_UTIL_H_

namespace hlcpp_benchmarks {

template <typename BuilderFunc>
bool BuilderBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  while (state->KeepRunning()) {
    builder();
  }
  return true;
}

}  // namespace hlcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_HLCPP_BUILDER_BENCHMARK_UTIL_H_
