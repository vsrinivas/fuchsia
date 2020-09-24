// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_WALKER_WALKER_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_WALKER_WALKER_BENCHMARK_UTIL_H_

#include <lib/fidl/llcpp/aligned.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/linearized_and_encoded.h>
#include <lib/fidl/visitor.h>
#include <lib/fidl/walker.h>

#include <perftest/perftest.h>

namespace walker_benchmarks {
namespace internal {

template <typename FidlType>
struct LinearizedResult {
  ::fidl::internal::LinearizeBuffer<FidlType> buffer;
  ::fidl::DecodedMessage<FidlType> message;
  zx_status_t status;
};

// Linearizes by encoding and then decoding into a linearized form.
// fidl::Linearize is being removed in favor of fidl::LinearizeAndEncode,
// so it is no longer possible to directly linearize arbitrary values.
// TODO(fxbug.dev/53743) Change the walker to walk encoded bytes.
template <typename FidlType>
LinearizedResult<FidlType> LinearizeForBenchmark(FidlType* value) {
  LinearizedResult<FidlType> result;
  auto encode_result = fidl::LinearizeAndEncode(value, result.buffer.buffer());
  if (encode_result.status != ZX_OK || encode_result.error != nullptr) {
    result.status = encode_result.status;
    return result;
  }
  auto decode_result = fidl::Decode(std::move(encode_result.message));
  if (decode_result.status != ZX_OK || decode_result.error != nullptr) {
    result.status = decode_result.status;
    return result;
  }
  result.status = ZX_OK;
  result.message = std::move(decode_result.message);
  return result;
}

void Walk(const fidl_type_t* fidl_type, uint8_t* data);
}  // namespace internal

template <typename FidlType, typename BuilderFunc>
bool WalkerBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  builder([state](FidlType value) {
    fidl::aligned<FidlType> aligned_value = std::move(value);
    auto linearize_result = internal::LinearizeForBenchmark<FidlType>(&aligned_value.value);
    ZX_ASSERT(linearize_result.status == ZX_OK);
    fidl::BytePart bytes = linearize_result.message.Release();

    while (state->KeepRunning()) {
      internal::Walk(FidlType::Type, bytes.data());
    }
  });

  return true;
}

}  // namespace walker_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_WALKER_WALKER_BENCHMARK_UTIL_H_
