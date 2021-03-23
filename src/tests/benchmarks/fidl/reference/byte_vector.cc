// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Benchmarks for a reference encoder / decoder specialized to ByteVector,
// as defined in the FIDL benchmark suite.

#include <cstdint>

#include "builder.h"
#include "decode_benchmark_util.h"
#include "encode_benchmark_util.h"

namespace {

template <size_t StackBufferSize>
bool EncodeByteVector(void* value, const char** error,
                      fit::function<void(const uint8_t*, size_t)> callback) {
  fidl_vector_t* vec = reinterpret_cast<fidl_vector_t*>(value);

  size_t count = vec->count;
  size_t aligned_size = FIDL_ALIGN(count);
  size_t needed_buffer_size = aligned_size + sizeof(fidl_vector_t);

  uint8_t buf[StackBufferSize];

  *reinterpret_cast<fidl_vector_t*>(buf) = fidl_vector_t{
      .count = count,
      .data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT),
  };
  if (count != 0) {
    if (vec->data == nullptr) {
      *error = "vector with null data had non-zero element count";
      return false;
    }
    // Zero the last 8 bytes which pad the end of the out of line block.
    *reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(buf) + needed_buffer_size - 8) = 0;
    memcpy(buf + sizeof(fidl_vector_t), vec->data, aligned_size);
  }

  callback(buf, needed_buffer_size);
  return true;
}

bool DecodeByteVector(uint8_t* bytes, size_t bytes_size, zx_handle_t* handles, size_t handles_size,
                      const char** error) {
  if (handles_size != 0) {
    *error = "no handles expected";
    return false;
  }

  fidl_vector_t* vec = reinterpret_cast<fidl_vector_t*>(bytes);
  if (unlikely(reinterpret_cast<uintptr_t>(vec->data) != FIDL_ALLOC_PRESENT)) {
    *error = "non-nullable vector missing out of line data";
    return false;
  }
  vec->data = bytes + sizeof(fidl_vector_t);
  return true;
}

bool BenchmarkEncodeByteVector16(perftest::RepeatState* state) {
  return encode_benchmark_util::EncodeBenchmark(state, benchmark_suite::Build_ByteVector_16,
                                                EncodeByteVector<32>);
}
bool BenchmarkEncodeByteVector256(perftest::RepeatState* state) {
  return encode_benchmark_util::EncodeBenchmark(state, benchmark_suite::Build_ByteVector_256,
                                                EncodeByteVector<272>);
}
bool BenchmarkEncodeByteVector4096(perftest::RepeatState* state) {
  return encode_benchmark_util::EncodeBenchmark(state, benchmark_suite::Build_ByteVector_4096,
                                                EncodeByteVector<4112>);
}
bool BenchmarkDecodeByteVector16(perftest::RepeatState* state) {
  return decode_benchmark_util::DecodeBenchmark(state, benchmark_suite::Build_ByteVector_16,
                                                DecodeByteVector);
}
bool BenchmarkDecodeByteVector256(perftest::RepeatState* state) {
  return decode_benchmark_util::DecodeBenchmark(state, benchmark_suite::Build_ByteVector_256,
                                                DecodeByteVector);
}
bool BenchmarkDecodeByteVector4096(perftest::RepeatState* state) {
  return decode_benchmark_util::DecodeBenchmark(state, benchmark_suite::Build_ByteVector_4096,
                                                DecodeByteVector);
}

void RegisterTests() {
  perftest::RegisterTest("Reference/Encode/ByteVector/16/WallTime/Steps",
                         BenchmarkEncodeByteVector16);
  perftest::RegisterTest("Reference/Encode/ByteVector/256/WallTime/Steps",
                         BenchmarkEncodeByteVector256);
  perftest::RegisterTest("Reference/Encode/ByteVector/4096/WallTime/Steps",
                         BenchmarkEncodeByteVector4096);
  perftest::RegisterTest("Reference/Decode/ByteVector/16/WallTime/Steps",
                         BenchmarkDecodeByteVector16);
  perftest::RegisterTest("Reference/Decode/ByteVector/256/WallTime/Steps",
                         BenchmarkDecodeByteVector256);
  perftest::RegisterTest("Reference/Decode/ByteVector/4096/WallTime/Steps",
                         BenchmarkDecodeByteVector4096);
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
