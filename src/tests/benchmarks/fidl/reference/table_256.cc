// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Benchmarks for a reference encoder / decoder specialized to Table256Struct
// as defined in the fidl benchmark suite.

#include <algorithm>
#include <cstdint>

#include "builder.h"
#include "decode_benchmark_util.h"

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

namespace {

bool DecodeTable256Struct(uint8_t* bytes, size_t bytes_size, zx_handle_t* handles,
                          size_t handles_size, const char** error) {
  fidl_vector_t* table_vec = reinterpret_cast<fidl_vector_t*>(bytes);
  if (unlikely(table_vec->data == nullptr && table_vec->count != 0)) {
    *error = "table with null data had non-zero element count";
    return false;
  }

  fidl_envelope_t* start_envelopes =
      reinterpret_cast<fidl_envelope_t*>(bytes + sizeof(fidl_vector_t));
  table_vec->data = start_envelopes;
  fidl_envelope_t* end_known_envelopes = start_envelopes + std::min(256ul, table_vec->count);
  fidl_envelope_t* end_all_envelopes = start_envelopes + table_vec->count;

  uint8_t* next_out_of_line = reinterpret_cast<uint8_t*>(end_all_envelopes);
  if (unlikely(reinterpret_cast<uint8_t*>(next_out_of_line) - bytes > int64_t(bytes_size))) {
    *error = "byte size exceeds available size";
    return false;
  }

  for (fidl_envelope_t* envelope = start_envelopes; envelope != end_known_envelopes; envelope++) {
    if (unlikely(envelope->num_handles != 0)) {
      *error = "incorrect num_handles in envelope";
      return false;
    }
    if (envelope->data == nullptr) {
      if (unlikely(envelope->num_bytes != 0)) {
        *error = "incorrect num_bytes in envelope";
        return false;
      }
    } else {
      if (unlikely(envelope->num_bytes != 8)) {
        *error = "incorrect num_bytes in envelope";
        return false;
      }
      if (unlikely((*reinterpret_cast<uint64_t*>(next_out_of_line) & 0xffffffffffffff00) != 0)) {
        *error = "invalid padding byte";
        return false;
      }
      envelope->data = next_out_of_line;
      next_out_of_line += 8;
      if (unlikely(reinterpret_cast<uint8_t*>(next_out_of_line) - bytes > int64_t(bytes_size))) {
        *error = "byte size exceeds available size";
        return false;
      }
    }
  }

  // Unknown envelopes
  uint32_t num_handles = 0;
  for (fidl_envelope_t* envelope = end_known_envelopes; envelope != end_all_envelopes; envelope++) {
    if (envelope->data == nullptr) {
      if (unlikely(envelope->num_bytes != 0)) {
        *error = "incorrect num_bytes in envelope";
        return false;
      }
      if (unlikely(envelope->num_handles != 0)) {
        *error = "incorrect num_handles in envelope";
        return false;
      }
    } else {
      size_t aligned_bytes = envelope->num_bytes;
      if (unlikely(!FidlIsAligned(reinterpret_cast<uint8_t*>(aligned_bytes)))) {
        *error = "incorrect num_bytes in envelope";
        return false;
      }
      num_handles += envelope->num_handles;
      envelope->data = next_out_of_line;
      next_out_of_line += aligned_bytes;
      if (unlikely(reinterpret_cast<uint8_t*>(next_out_of_line) - bytes > int64_t(bytes_size))) {
        *error = "byte size exceeds available size";
        return false;
      }
    }
  }
#ifdef __Fuchsia__
  zx_handle_close_many(handles, num_handles);
#endif
  return true;
}

bool BenchmarkDecodeTableAllSet256(perftest::RepeatState* state) {
  return decode_benchmark_util::DecodeBenchmark(state, benchmark_suite::BuildTable_AllSet_256,
                                                DecodeTable256Struct);
}
bool BenchmarkDecodeTableUnset256(perftest::RepeatState* state) {
  return decode_benchmark_util::DecodeBenchmark(state, benchmark_suite::BuildTable_Unset_256,
                                                DecodeTable256Struct);
}
bool BenchmarkDecodeTableSingleSet_1_of_256(perftest::RepeatState* state) {
  return decode_benchmark_util::DecodeBenchmark(
      state, benchmark_suite::BuildTable_SingleSet_1_of_256, DecodeTable256Struct);
}
bool BenchmarkDecodeTableSingleSet_16_of_256(perftest::RepeatState* state) {
  return decode_benchmark_util::DecodeBenchmark(
      state, benchmark_suite::BuildTable_SingleSet_16_of_256, DecodeTable256Struct);
}
bool BenchmarkDecodeTableSingleSet_256_of_256(perftest::RepeatState* state) {
  return decode_benchmark_util::DecodeBenchmark(
      state, benchmark_suite::BuildTable_SingleSet_256_of_256, DecodeTable256Struct);
}

void RegisterTests() {
  perftest::RegisterTest("Reference/Decode/Table/AllSet/256/Steps", BenchmarkDecodeTableAllSet256);
  perftest::RegisterTest("Reference/Decode/Table/Unset/256/Steps", BenchmarkDecodeTableUnset256);
  perftest::RegisterTest("Reference/Decode/Table/SingleSet/1_of_256/Steps",
                         BenchmarkDecodeTableSingleSet_1_of_256);
  perftest::RegisterTest("Reference/Decode/Table/SingleSet/16_of_256/Steps",
                         BenchmarkDecodeTableSingleSet_16_of_256);
  perftest::RegisterTest("Reference/Decode/Table/SingleSet/256_of_256/Steps",
                         BenchmarkDecodeTableSingleSet_256_of_256);
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
