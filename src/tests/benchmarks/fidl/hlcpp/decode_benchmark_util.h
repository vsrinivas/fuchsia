// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_HLCPP_DECODE_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_HLCPP_DECODE_BENCHMARK_UTIL_H_

#include "coding_table.h"

namespace hlcpp_benchmarks {

template <typename BuilderFunc>
bool DecodeBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc>;
  FidlType obj = builder();

  fidl::Encoder enc(fidl::Encoder::NoHeader::NO_HEADER);
  auto offset = enc.Alloc(EncodedSize<FidlType>);
  obj.Encode(&enc, offset);
  fidl::Message encode_msg = enc.GetMessage();

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("Decode/WallTime");
  state->DeclareStep("Teardown/WallTime");

  std::vector<uint8_t> buffer(encode_msg.bytes().actual());
  while (state->KeepRunning()) {
    memcpy(buffer.data(), encode_msg.bytes().data(), encode_msg.bytes().actual());

    state->NextStep();  // End: Setup. Begin: Decode.

    {
      uint32_t size = buffer.size();
      fidl::Message decode_msg(fidl::BytePart(buffer.data(), size, size), fidl::HandlePart());
      ZX_ASSERT(ZX_OK == decode_msg.Decode(FidlType::FidlType, nullptr));
      fidl::Decoder decoder(std::move(decode_msg));
      FidlType output;
      FidlType::Decode(&decoder, &output, 0);
    }

    state->NextStep();  // End: Decode. Begin: Teardown.
  }

  return true;
}

}  // namespace hlcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_HLCPP_DECODE_BENCHMARK_UTIL_H_
