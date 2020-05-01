// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_LLCPP_MEMCPY_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_LLCPP_MEMCPY_BENCHMARK_UTIL_H_

#include "encode_benchmark_util.h"

namespace llcpp_benchmarks {

template <typename BuilderFunc>
bool MemcpyBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc>;
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  fidl::aligned<FidlType> aligned_value = builder();
  uint8_t linearize_buffer[BufferSize<FidlType>];
  auto benchmark_linearize_result = Linearize(&aligned_value.value, linearize_buffer);
  auto& linearize_result = benchmark_linearize_result.result;
  ZX_ASSERT(linearize_result.status == ZX_OK && linearize_result.error == nullptr);
  fidl::BytePart bytes = linearize_result.message.Release();

  std::vector<uint8_t> target_buf(bytes.actual());

  while (state->KeepRunning()) {
    memcpy(target_buf.data(), bytes.data(), bytes.actual());
  }

  return true;
}

}  // namespace llcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_LLCPP_MEMCPY_BENCHMARK_UTIL_H_
