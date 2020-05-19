// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_WALKER_WALKER_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_WALKER_WALKER_BENCHMARK_UTIL_H_

#include <lib/fidl/llcpp/aligned.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/linearized.h>
#include <lib/fidl/visitor.h>
#include <lib/fidl/walker.h>

#include <perftest/perftest.h>

namespace walker_benchmarks {
namespace internal {
void Walk(const fidl_type_t* fidl_type, uint8_t* data);
}  // namespace internal

template <typename BuilderFunc>
bool WalkerBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc>;
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  fidl::aligned<FidlType> aligned_value = builder();
  auto linearized = fidl::internal::Linearized<FidlType>(&aligned_value.value);
  auto& linearize_result = linearized.result();
  ZX_ASSERT(linearize_result.status == ZX_OK && linearize_result.error == nullptr);
  fidl::BytePart bytes = linearize_result.message.Release();

  while (state->KeepRunning()) {
    internal::Walk(FidlType::Type, bytes.data());
  }

  return true;
}

}  // namespace walker_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_WALKER_WALKER_BENCHMARK_UTIL_H_
