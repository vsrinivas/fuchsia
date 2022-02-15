// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_CPP_BUILDER_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_CPP_BUILDER_BENCHMARK_UTIL_H_

#include <perftest/perftest.h>

namespace cpp_benchmarks {

template <typename BuilderFunc, typename SetupFunc>
bool BuilderBenchmark(perftest::RepeatState* state, BuilderFunc builder, SetupFunc setup) {
  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("Build/WallTime");
  state->DeclareStep("Teardown/WallTime");
  while (state->KeepRunning()) {
    auto buildContext = setup();

    state->NextStep();  // End: Setup. Begin: Build

    [[maybe_unused]] auto result = builder(buildContext);

    state->NextStep();  // End: Build. Start: Teardown

    // handles inside the constructed object are destroyed here as `result` goes
    // out of scope
  }
  return true;
}

}  // namespace cpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_CPP_BUILDER_BENCHMARK_UTIL_H_
