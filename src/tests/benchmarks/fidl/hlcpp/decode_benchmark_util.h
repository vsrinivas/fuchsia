// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_HLCPP_DECODE_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_HLCPP_DECODE_BENCHMARK_UTIL_H_

#include "coding_table.h"
#include "lib/fidl/cpp/message.h"
#include "lib/fidl/internal.h"

namespace hlcpp_benchmarks {

namespace {
constexpr uint64_t kOrdinal = 1234;
constexpr fidl_message_header_t kV2Header = {
    .flags = {FIDL_MESSAGE_HEADER_FLAGS_0_USE_VERSION_V2},
    .magic_number = kFidlWireFormatMagicNumberInitial,
    .ordinal = kOrdinal,
};
}  // namespace

template <typename BuilderFunc>
bool DecodeBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc>;

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("Decode/WallTime");
  state->DeclareStep("Teardown/WallTime");

  std::vector<uint8_t> buffer;
  std::vector<zx_handle_info_t> handle_infos;
  while (state->KeepRunning()) {
    // construct a new object each iteration so that the handle close cost is included in the
    // decode time.
    FidlType obj = builder();

    fidl::BodyEncoder enc(::fidl::internal::WireFormatVersion::kV2);
    auto offset = enc.Alloc(EncodedSize<FidlType>);
    obj.Encode(&enc, offset);
    fidl::HLCPPOutgoingBody encode_body = enc.GetBody();

    buffer.resize(encode_body.bytes().actual());
    memcpy(buffer.data(), encode_body.bytes().data(), encode_body.bytes().actual());
    handle_infos.resize(encode_body.handles().actual());
    ZX_ASSERT(ZX_OK == FidlHandleDispositionsToHandleInfos(encode_body.handles().data(),
                                                           handle_infos.data(),
                                                           encode_body.handles().actual()));

    state->NextStep();  // End: Setup. Begin: Decode.

    {
      fidl::HLCPPIncomingBody decode_body(
          fidl::BytePart(buffer.data(), static_cast<uint32_t>(buffer.size()),
                         static_cast<uint32_t>(buffer.size())),
          fidl::HandleInfoPart(handle_infos.data(), static_cast<uint32_t>(handle_infos.size()),
                               static_cast<uint32_t>(handle_infos.size())));
      const char* error_msg;
      const auto metadata = fidl::internal::WireFormatMetadata::FromTransactionalHeader(kV2Header);

      ZX_ASSERT_MSG(ZX_OK == decode_body.Decode(metadata, FidlType::FidlType, &error_msg), "%s",
                    error_msg);
      fidl::Decoder decoder(std::move(decode_body));
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
