// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Benchmarks for a reference encoder / decoder specialized to PaddedStructTree.

#include <cstdint>

#include "builder.h"
#include "decode_benchmark_util.h"
#include "encode_benchmark_util.h"

namespace {

bool EncodePaddedStructTree(void* value, const char** error,
                            fit::function<void(const uint8_t*, size_t)> callback) {
  uint8_t buf[sizeof(test_benchmarkfidl::wire::PaddedStructTree8)];

  uint64_t mask = 0xffffffff000000ff;
  uint64_t* in = reinterpret_cast<uint64_t*>(value);
  uint64_t* in_end = reinterpret_cast<uint64_t*>(
      reinterpret_cast<test_benchmarkfidl::wire::PaddedStructTree8*>(value) + 1);
  uint64_t* out = reinterpret_cast<uint64_t*>(buf);
  for (; in < in_end; ++in, ++out) {
    // Note: padding bytes are blindly zeroed rather than checking that existing padding data is
    // zero because LLCPP does not require that padding bytes in values are zero.
    *out = *in & mask;
  }
  callback(buf, sizeof(buf));
  return true;
}

bool DecodePaddedStructTree(uint8_t* bytes, size_t bytes_size, zx_handle_t* handles,
                            size_t handles_size, const char** error) {
  if (handles_size != 0) {
    *error = "no handles expected";
    return false;
  }

  uint64_t mask = 0x00000000ffffff00;
  uint64_t* cur = reinterpret_cast<uint64_t*>(bytes);
  uint64_t* end =
      reinterpret_cast<uint64_t*>(bytes + sizeof(test_benchmarkfidl::wire::PaddedStructTree8));
  for (; cur < end; ++cur) {
    if ((*cur & mask) != 0) {
      *error = "non-zero padding byte";
      return false;
    }
  }

  return true;
}

bool BenchmarkEncodePaddedStructTree(perftest::RepeatState* state) {
  return encode_benchmark_util::EncodeBenchmark(
      state, benchmark_suite::Build_PaddedStructTree_Depth8, EncodePaddedStructTree);
}
bool BenchmarkDecodePaddedStructTree(perftest::RepeatState* state) {
  return decode_benchmark_util::DecodeBenchmark(
      state, benchmark_suite::Build_PaddedStructTree_Depth8, DecodePaddedStructTree);
}

void RegisterTests() {
  perftest::RegisterTest("Reference/Encode/PaddedStructTree/Depth8",
                         BenchmarkEncodePaddedStructTree);
  perftest::RegisterTest("Reference/Decode/PaddedStructTree/Depth8",
                         BenchmarkDecodePaddedStructTree);
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
