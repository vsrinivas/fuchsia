// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_CPP_ENCODE_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_CPP_ENCODE_BENCHMARK_UTIL_H_

#include <lib/fidl/cpp/natural_types.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <iostream>
#include <type_traits>

#include <perftest/perftest.h>

namespace cpp_benchmarks {

template <typename BuilderFunc>
bool EncodeBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc>;
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("Encode/WallTime");
  state->DeclareStep("Teardown/WallTime");

  while (state->KeepRunning()) {
    auto value = builder();

    state->NextStep();  // End: Setup. Begin: Encode.

    fidl::internal::EncodeResult result = fidl::internal::EncodeIntoResult(value);

    state->NextStep();  // End: Encode. Begin: Teardown.

    ZX_ASSERT(result.message().ok());
  }

  return true;
}

}  // namespace cpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_CPP_ENCODE_BENCHMARK_UTIL_H_
