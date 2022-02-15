// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_CPP_DECODE_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_CPP_DECODE_BENCHMARK_UTIL_H_

#include <lib/fidl/cpp/natural_types.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <iostream>
#include <type_traits>

#include <perftest/perftest.h>

namespace cpp_benchmarks {

const fidl::internal::WireFormatMetadata kV2WireformatMetadata =
    fidl::internal::WireFormatMetadata::FromTransactionalHeader({
        .flags = {FIDL_MESSAGE_HEADER_FLAGS_0_USE_VERSION_V2, 0, 0},
        .magic_number = kFidlWireFormatMagicNumberInitial,
    });

template <typename BuilderFunc>
bool DecodeBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc>;
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("Decode/WallTime");
  state->DeclareStep("Teardown/WallTime");

  while (state->KeepRunning()) {
    auto value = builder();
    fidl::internal::EncodeResult encode_result = fidl::internal::EncodeIntoResult(value);
    ZX_ASSERT(encode_result.message().ok());

    // Convert the outgoing message to incoming which is suitable for decoding.
    // This may involve expensive allocations and copies.
    // This step does not happen in production (we would receive an incoming message
    // from the channel), hence not counted as part of decode time.
    auto converted = fidl::OutgoingToIncomingMessage(encode_result.message());

    state->NextStep();  // End: Setup. Begin: Decode.

    {
      auto result = fidl::internal::DecodeFrom<FidlType>(std::move(converted.incoming_message()),
                                                         kV2WireformatMetadata);
      ZX_DEBUG_ASSERT(result.is_ok());
      // Include time taken to close handles in |FidlType|.
    }

    state->NextStep();  // End: Decode. Begin: Teardown.
  }
  return true;
}

}  // namespace cpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_CPP_DECODE_BENCHMARK_UTIL_H_
