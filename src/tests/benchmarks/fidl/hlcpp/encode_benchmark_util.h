// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_HLCPP_ENCODE_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_HLCPP_ENCODE_BENCHMARK_UTIL_H_

#include "coding_table.h"

namespace hlcpp_benchmarks {

template <typename BuilderFunc>
bool EncodeBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc>;

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("Encode/WallTime");
  state->DeclareStep("Teardown/WallTime");

  while (state->KeepRunning()) {
    FidlType obj = builder();

    state->NextStep();  // End: Setup. Begin: Encode.

    {
      fidl::Encoder enc(fidl::Encoder::NoHeader::NO_HEADER);
      auto offset = enc.Alloc(fidl::EncodingInlineSize<FidlType, fidl::Encoder>(&enc));
      obj.Encode(&enc, offset);
      fidl::HLCPPOutgoingMessage msg = enc.GetMessage();
      ZX_ASSERT(ZX_OK == msg.Validate(FidlType::FidlType, nullptr));
    }

    state->NextStep();  // End: Encode. Begin: Teardown.
  }
  return true;
}

}  // namespace hlcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_HLCPP_ENCODE_BENCHMARK_UTIL_H_
