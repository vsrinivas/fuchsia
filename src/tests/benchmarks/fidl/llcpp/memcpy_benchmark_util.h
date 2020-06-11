// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_LLCPP_MEMCPY_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_LLCPP_MEMCPY_BENCHMARK_UTIL_H_

#include <lib/fidl/llcpp/coding.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <type_traits>

#include <perftest/perftest.h>

namespace llcpp_benchmarks {

template <typename BuilderFunc>
bool MemcpyBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc>;
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  fidl::aligned<FidlType> aligned_value = builder();
  ::fidl::internal::LinearizeBuffer<FidlType> buf;
  auto encode_result = ::fidl::LinearizeAndEncode<FidlType>(&aligned_value.value, buf.buffer());
  ZX_ASSERT(encode_result.status == ZX_OK && encode_result.error == nullptr);
  const fidl::BytePart& bytes = encode_result.message.bytes();

  std::vector<uint8_t> target_buf(bytes.actual());

  while (state->KeepRunning()) {
    memcpy(target_buf.data(), bytes.data(), bytes.actual());
  }

  return true;
}

}  // namespace llcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_LLCPP_MEMCPY_BENCHMARK_UTIL_H_
