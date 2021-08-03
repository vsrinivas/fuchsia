// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_DECODE_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_DECODE_BENCHMARK_UTIL_H_

#include <lib/fidl/llcpp/arena.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/message.h>
#include <zircon/fidl.h>
#include <zircon/status.h>

#include <iomanip>
#include <iostream>
#include <vector>

#include <perftest/perftest.h>

namespace decode_benchmark_util {

template <typename BuilderFunc, typename DecodeFunc>
bool DecodeBenchmark(perftest::RepeatState* state, BuilderFunc builder, DecodeFunc decode) {
  using FidlType = std::invoke_result_t<BuilderFunc, fidl::AnyArena&>;
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  fidl::Arena<65536> allocator;
  FidlType aligned_value = builder(allocator);
  fidl::OwnedEncodedMessage<FidlType> encoded(&aligned_value);
  ZX_ASSERT(encoded.ok());

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("Decode/WallTime");
  state->DeclareStep("Teardown/WallTime");

  fidl::OutgoingMessage::CopiedBytes bytes;
  while (state->KeepRunning()) {
    bytes = encoded.GetOutgoingMessage().CopyBytes();

    state->NextStep();  // End: Setup. Begin: Decode.

    const char* error;
    if (!decode(bytes.data(), bytes.size(), nullptr, 0, &error)) {
      std::cout << "error in decode benchmark: " << error << std::endl;
      return false;
    }

    state->NextStep();  // End: Decode. Begin: Teardown.
  }

  // Reencode the decoded result and compare against the initial (expected) encode_result.
  fidl::OwnedEncodedMessage<FidlType> reencoded(reinterpret_cast<FidlType*>(bytes.data()));
  if (!reencoded.ok()) {
    std::cout << "fidl::Encode failed with error: " << reencoded.error() << std::endl;
    return false;
  }
  auto reencoded_bytes = reencoded.GetOutgoingMessage().CopyBytes();
  auto encoded_bytes = encoded.GetOutgoingMessage().CopyBytes();

  if (encoded_bytes.size() != reencoded_bytes.size()) {
    std::cout << "output size mismatch - reencoded size was " << encoded_bytes.size()
              << " but expected encode result size was" << encoded_bytes.size() << std::endl;
    return false;
  }
  bool success = true;
  for (uint32_t i = 0; i < encoded_bytes.size(); ++i) {
    if (encoded_bytes.data()[i] != reencoded_bytes.data()[i]) {
      std::cout << "At offset " << i << " reencoded got 0x" << std::setw(2) << std::setfill('0')
                << std::hex << int(reencoded_bytes.data()[i]) << " but expected was 0x"
                << int(encoded_bytes.data()[i]) << std::dec << std::endl;
      success = false;
    }
  }
  return success;
}

}  // namespace decode_benchmark_util

#endif  // SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_DECODE_BENCHMARK_UTIL_H_
