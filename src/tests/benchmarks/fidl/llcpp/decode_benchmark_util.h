// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_LLCPP_DECODE_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_LLCPP_DECODE_BENCHMARK_UTIL_H_

namespace llcpp_benchmarks {

template <typename FidlType>
bool DecodeBenchmark(perftest::RepeatState* state, std::vector<uint8_t> bytes) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("Decode/WallTime");
  state->DeclareStep("Destructors/WallTime");

  std::vector<uint8_t> test_data(bytes.size());
  while (state->KeepRunning()) {
    // TODO(fxb/49815) Move the memcpy out of the main loop.
    memcpy(test_data.data(), bytes.data(), bytes.size());
    fidl::EncodedMessage<FidlType> message(
        fidl::BytePart(&test_data[0], test_data.size(), test_data.size()));

    state->NextStep();  // Done with setup. Next: Decode.

    auto decode_result = fidl::Decode(std::move(message));
    ZX_ASSERT(decode_result.status == ZX_OK);

    state->NextStep();  // Next: Destructors called.
  }
  return true;
}

}  // namespace llcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_LLCPP_DECODE_BENCHMARK_UTIL_H_
