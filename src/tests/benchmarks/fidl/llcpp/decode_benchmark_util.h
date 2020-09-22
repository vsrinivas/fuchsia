// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_LLCPP_DECODE_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_LLCPP_DECODE_BENCHMARK_UTIL_H_

#include <lib/fidl/llcpp/coding.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
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

  std::vector<uint8_t> test_bytes;
  std::vector<zx_handle_t> test_handles;
  while (state->KeepRunning()) {
    // construct a new object each iteration, so that the handle close cost is included in the
    // decode time.
    fidl::aligned<FidlType> aligned_value = builder();
    // encode the value.
    ::fidl::internal::LinearizeBuffer<FidlType> buf;
    auto encode_result = ::fidl::LinearizeAndEncode<FidlType>(&aligned_value.value, buf.buffer());
    ZX_ASSERT(encode_result.status == ZX_OK && encode_result.error == nullptr);
    const fidl::BytePart& bytes = encode_result.message.bytes();
    const fidl::HandlePart& handles = encode_result.message.handles();

    test_bytes.resize(bytes.size());
    test_handles.resize(handles.size());
    memcpy(test_bytes.data(), bytes.data(), bytes.actual());
    memcpy(test_handles.data(), handles.data(), handles.actual() * sizeof(zx_handle_t));
    fidl_msg_t msg = {
        .bytes = &test_bytes[0],
        .handles = &test_handles[0],
        .num_bytes = (uint32_t)test_bytes.size(),
        .num_handles = (uint32_t)test_handles.size(),
    };

    state->NextStep();  // End: Setup. Begin: Decode.

    {
      fidl::EncodedMessage<FidlType> message(&msg);
      auto decode_result = fidl::Decode(std::move(message));
      ZX_ASSERT(decode_result.status == ZX_OK);
    }

    state->NextStep();  // End: Decode. Begin: Teardown.
  }
  return true;
}

}  // namespace llcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_LLCPP_DECODE_BENCHMARK_UTIL_H_
