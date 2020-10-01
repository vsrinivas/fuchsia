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

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("Decode/WallTime");
  state->DeclareStep("Teardown/WallTime");

  std::vector<uint8_t> buffer;
  std::vector<zx_handle_t> handles;
  while (state->KeepRunning()) {
    // construct a new object each iteration so that the handle close cost is included in the
    // decode time.
    FidlType obj = builder();

    fidl::Encoder enc(fidl::Encoder::NoHeader::NO_HEADER);
    auto offset = enc.Alloc(EncodedSize<FidlType>);
    obj.Encode(&enc, offset);
    fidl::Message encode_msg = enc.GetMessage();

    buffer.resize(encode_msg.bytes().actual());
    handles.resize(encode_msg.handles().actual());
    memcpy(buffer.data(), encode_msg.bytes().data(), encode_msg.bytes().actual());
    memcpy(handles.data(), encode_msg.handles().data(),
           encode_msg.handles().actual() * sizeof(zx_handle_t));

    state->NextStep();  // End: Setup. Begin: Decode.

    {
      fidl::Message decode_msg(
          fidl::BytePart(buffer.data(), static_cast<uint32_t>(buffer.size()),
                         static_cast<uint32_t>(buffer.size())),
          fidl::HandlePart(handles.data(), static_cast<uint32_t>(handles.size()),
                           static_cast<uint32_t>(handles.size())));
      const char* error_msg;
      ZX_ASSERT_MSG(ZX_OK == decode_msg.Decode(FidlType::FidlType, &error_msg), "%s", error_msg);
      fidl::Decoder decoder(std::move(decode_msg));
      FidlType output;
      FidlType::Decode(&decoder, &output, 0);
      // Note: `output` goes out of scope here, so we include its destruction time in the
      // Decode/Walltime step, including closing any handles.
    }

    state->NextStep();  // End: Decode. Begin: Teardown.
  }

  return true;
}

}  // namespace hlcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_HLCPP_DECODE_BENCHMARK_UTIL_H_
