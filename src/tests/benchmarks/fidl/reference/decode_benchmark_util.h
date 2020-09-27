// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_DECODE_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_DECODE_BENCHMARK_UTIL_H_

#include <lib/fidl/internal.h>
#include <zircon/fidl.h>
#include <zircon/status.h>

#include <iomanip>
#include <iostream>

#include <perftest/perftest.h>

namespace decode_benchmark_util {

template <typename BuilderFunc, typename DecodeFunc>
bool DecodeBenchmark(perftest::RepeatState* state, BuilderFunc builder, DecodeFunc decode) {
  using FidlType = std::invoke_result_t<BuilderFunc>;
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  fidl::aligned<FidlType> aligned_value = builder();
  ::fidl::internal::LinearizeBuffer<FidlType> buf;
  auto encode_result = ::fidl::LinearizeAndEncode<FidlType>(&aligned_value.value, buf.buffer());
  ZX_ASSERT(encode_result.status == ZX_OK && encode_result.error == nullptr);
  const fidl::BytePart& bytes = encode_result.message.bytes();

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("Decode/WallTime");
  state->DeclareStep("Teardown/WallTime");

  std::vector<uint8_t> test_data(bytes.size());
  while (state->KeepRunning()) {
    memcpy(test_data.data(), bytes.data(), bytes.size());

    state->NextStep();  // End: Setup. Begin: Decode.

    const char* error;
    if (!decode(&test_data[0], test_data.size(), nullptr, 0, &error)) {
      std::cout << "error in decode benchmark: " << error << std::endl;
      return false;
    }

    state->NextStep();  // End: Decode. Begin: Teardown.
  }

  // Encode the result with fidl::Encode and compare against the expected encode_result.
  auto reference_encode_result = fidl::Encode(fidl::DecodedMessage<FidlType>(
      fidl::BytePart(test_data.data(), static_cast<unsigned int>(test_data.size()),
                     static_cast<unsigned int>(test_data.size()))));
  if (reference_encode_result.status != ZX_OK) {
    std::cout << "fidl::Encode failed with error: " << encode_result.error << std::endl;
    return false;
  }
  auto& expected_bytes = encode_result.message.bytes();
  auto& reference_bytes = reference_encode_result.message.bytes();
  if (expected_bytes.actual() != reference_bytes.actual()) {
    std::cout << "output size mismatch - encoded reference size was " << reference_bytes.actual()
              << " but expected encode result size was" << expected_bytes.actual() << std::endl;
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

}  // namespace decode_benchmark_util

#endif  // SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_DECODE_BENCHMARK_UTIL_H_
