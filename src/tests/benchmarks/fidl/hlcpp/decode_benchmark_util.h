// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_HLCPP_DECODE_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_HLCPP_DECODE_BENCHMARK_UTIL_H_

#include "coding_table.h"

namespace hlcpp_benchmarks {

template <typename FidlType>
bool DecodeBenchmark(perftest::RepeatState* state, FidlType obj) {
  constexpr uint32_t ordinal = 0xfefefefe;
  fidl::Encoder enc(ordinal);

  auto offset = enc.Alloc(EncodedSize<FidlType>);
  obj.Encode(&enc, offset);
  fidl::Message encode_msg = enc.GetMessage();
  ZX_ASSERT(ZX_OK == encode_msg.Validate(&FidlTypeWithHeader<FidlType>, nullptr));

  // TODO(fxb/50213) This decodes the message header - it may throw off timing.
  std::vector<uint8_t> buffer(encode_msg.bytes().capacity());
  while (state->KeepRunning()) {
    memcpy(buffer.data(), encode_msg.bytes().data(), encode_msg.bytes().actual());
    fidl::Message decode_msg(
        fidl::BytePart(buffer.data(), encode_msg.bytes().capacity(), encode_msg.bytes().actual()),
        fidl::HandlePart());

    ZX_ASSERT(ZX_OK == decode_msg.Decode(&FidlTypeWithHeader<FidlType>, nullptr));
    fidl::Decoder decoder(std::move(decode_msg));
    FidlType output;
    FidlType::Decode(&decoder, &output, 0);
  }

  return true;
}

}  // namespace hlcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_HLCPP_DECODE_BENCHMARK_UTIL_H_
