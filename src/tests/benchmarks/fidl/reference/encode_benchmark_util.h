// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_ENCODE_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_ENCODE_BENCHMARK_UTIL_H_

#include <lib/fidl/llcpp/arena.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/message.h>
#include <zircon/fidl.h>

#include <cstdint>
#include <iostream>

#include <perftest/perftest.h>

namespace encode_benchmark_util {

template <typename BuilderFunc, typename EncodeFunc>
bool EncodeBenchmark(perftest::RepeatState* state, BuilderFunc builder, EncodeFunc encode) {
  using FidlType = std::invoke_result_t<BuilderFunc, fidl::AnyArena&>;
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("Encode/WallTime");
  state->DeclareStep("Teardown/WallTime");

  while (state->KeepRunning()) {
    fidl::Arena<65536> allocator;
    FidlType aligned_value = builder(allocator);

    state->NextStep();  // End: Setup. Begin: Encode.

    const char* error;
    if (!encode(&aligned_value, &error,
                [](const uint8_t* bytes, size_t) { perftest::DoNotOptimize(bytes); })) {
      std::cout << "error in encode benchmark: " << error << std::endl;
      return false;
    }

    state->NextStep();  // End: Encode. Begin: Teardown.
  }

  // Encode the input with fidl::Encode and compare againt encode().
  fidl::Arena<65536> allocator;
  FidlType aligned_value = builder(allocator);
  ::fidl::OwnedEncodedMessage<FidlType> encoded(&aligned_value);
  ZX_ASSERT(encoded.ok());

  fidl::Arena<65536> allocator2;
  aligned_value = builder(allocator2);
  std::vector<uint8_t> reference_bytes;
  const char* error;
  if (!encode(&aligned_value, &error, [&reference_bytes](const uint8_t* bytes, size_t size) {
        reference_bytes.resize(size);
        memcpy(reference_bytes.data(), bytes, size);
      })) {
    std::cout << "encode failed with error: " << error << std::endl;
    return false;
  }

  auto expected_bytes = encoded.GetOutgoingMessage().CopyBytes();
  if (expected_bytes.size() != reference_bytes.size()) {
    std::cout << "output size mismatch - encoded reference size was " << reference_bytes.size()
              << " but expected encode result size was" << expected_bytes.size() << std::endl;
    return false;
  }
  bool success = true;
  for (size_t i = 0; i < expected_bytes.size(); i++) {
    if (expected_bytes.data()[i] != reference_bytes.data()[i]) {
      std::cout << "At offset " << i << " reference got 0x" << std::setw(2) << std::setfill('0')
                << std::hex << int(reference_bytes.data()[i]) << " but fidl::Decode got 0x"
                << int(expected_bytes.data()[i]) << std::dec << std::endl;
      success = false;
    }
  }
  return success;
}

}  // namespace encode_benchmark_util

#endif  // SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_ENCODE_BENCHMARK_UTIL_H_
