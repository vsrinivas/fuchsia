// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_LLCPP_ENCODE_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_LLCPP_ENCODE_BENCHMARK_UTIL_H_

#include <lib/fidl/llcpp/arena.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/message.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <iostream>
#include <type_traits>

#include <perftest/perftest.h>

namespace llcpp_benchmarks {

template <typename BuilderFunc>
bool EncodeBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc, fidl::AnyArena&>;
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("Encode/WallTime");
  state->DeclareStep("Teardown/WallTime");

  while (state->KeepRunning()) {
    fidl::Arena<65536> allocator;
    FidlType aligned_value = builder(allocator);

    state->NextStep();  // End: Setup. Begin: Encode.

    {
      FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT
      ::fidl::OwnedEncodedMessage<FidlType> encoded(fidl::internal::AllowUnownedInputRef{},
                                                    &aligned_value);
      if (!encoded.ok()) {
        std::cerr << "Unexpected error: " << encoded.error() << std::endl;
      }
      ZX_ASSERT(encoded.ok());
    }

    state->NextStep();  // End: Encode. Begin: Teardown.
  }

  return true;
}

}  // namespace llcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_LLCPP_ENCODE_BENCHMARK_UTIL_H_
