// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_LLCPP_DECODE_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_LLCPP_DECODE_BENCHMARK_UTIL_H_

#include <lib/fidl/llcpp/coding.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <iostream>
#include <type_traits>

#include <perftest/perftest.h>

namespace llcpp_benchmarks {

template <typename BuilderFunc>
bool DecodeBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc>;
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("Decode/WallTime");
  state->DeclareStep("Teardown/WallTime");

  while (state->KeepRunning()) {
    // construct a new object each iteration, so that the handle close cost is included in the
    // decode time.
    fidl::aligned<FidlType> aligned_value = builder();
    // encode the value.
    fidl::OwnedOutgoingMessage<FidlType> encoded(&aligned_value.value);
    if (encoded.error() != nullptr) {
      std::cerr << "Unexpected error: " << encoded.error() << '\n';
    }
    ZX_ASSERT(encoded.ok());

    state->NextStep();  // End: Setup. Begin: Decode.

    {
      auto decoded = fidl::IncomingMessage<FidlType>::FromOutgoingWithRawHandleCopy(&encoded);
      ZX_ASSERT(decoded.ok());
    }

    state->NextStep();  // End: Decode. Begin: Teardown.
  }
  return true;
}

}  // namespace llcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_LLCPP_DECODE_BENCHMARK_UTIL_H_
