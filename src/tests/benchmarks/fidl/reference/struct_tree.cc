// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Benchmarks for a reference encoder / decoder specialized to PaddedStructTree.

#include <cstdint>

#include "builder.h"
#include "decode_benchmark_util.h"
#include "encode_benchmark_util.h"

namespace {

bool EncodeStructTree(void* value, const char** error,
                      fit::function<void(const uint8_t*, size_t)> callback) {
  uint8_t buf[sizeof(test_benchmarkfidl::wire::StructTree8)];
  memcpy(buf, value, sizeof(buf));

  callback(buf, sizeof(buf));
  return true;
}

bool DecodeStructTree(uint8_t* bytes, size_t bytes_size, zx_handle_t* handles, size_t handles_size,
                      const char** error) {
  if (handles_size != 0) {
    *error = "no handles expected";
    return false;
  }
  return true;
}

bool BenchmarkEncodeStructTree(perftest::RepeatState* state) {
  return encode_benchmark_util::EncodeBenchmark(state, benchmark_suite::Build_StructTree_Depth8,
                                                EncodeStructTree);
}
bool BenchmarkDecodeStructTree(perftest::RepeatState* state) {
  return decode_benchmark_util::DecodeBenchmark(state, benchmark_suite::Build_StructTree_Depth8,
                                                DecodeStructTree);
}

void RegisterTests() {
  perftest::RegisterTest("Reference/Encode/StructTree/Depth8", BenchmarkEncodeStructTree);
  perftest::RegisterTest("Reference/Decode/StructTree/Depth8", BenchmarkDecodeStructTree);
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
