// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_HLCPP_ENCODE_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_HLCPP_ENCODE_BENCHMARK_UTIL_H_

#include "coding_table.h"

namespace hlcpp_benchmarks {

template <typename FidlType>
bool EncodeBenchmark(perftest::RepeatState* state, FidlType obj) {
  constexpr uint32_t ordinal = 0xfefefefe;
  while (state->KeepRunning()) {
    fidl::Encoder enc(ordinal);
    auto offset = enc.Alloc(fidl::EncodingInlineSize<FidlType, fidl::Encoder>(&enc));
    obj.Encode(&enc, offset);
    fidl::Message msg = enc.GetMessage();
    ZX_ASSERT(ZX_OK == msg.Validate(&FidlTypeWithHeader<FidlType>, nullptr));
  }
  return true;
}

}  // namespace hlcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_HLCPP_ENCODE_BENCHMARK_UTIL_H_
