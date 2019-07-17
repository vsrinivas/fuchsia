// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fake_codec_adapter.h>
#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/mediacodec/cpp/fidl.h>
#include <gtest/gtest.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/gtest/real_loop_fixture.h>

#include "lib/media/codec_impl/codec_impl.h"

namespace {

constexpr uint32_t kInputMinBufferCountForCamping = 1;

auto CreateDecoderParams() {
  fuchsia::mediacodec::CreateDecoder_Params params;

  params.mutable_input_details()->set_format_details_version_ordinal(0);
  return params;
}

auto CreateStreamBufferPartialSettings(
    uint64_t buffer_lifetime_ordinal, const fuchsia::media::StreamBufferConstraints& constraints,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  constexpr uint64_t kBufferConstraintsVersionOrdinal = 1;

  fuchsia::media::StreamBufferPartialSettings settings;

  settings.set_buffer_lifetime_ordinal(buffer_lifetime_ordinal)
      .set_buffer_constraints_version_ordinal(kBufferConstraintsVersionOrdinal)
      .set_single_buffer_mode(constraints.default_settings().single_buffer_mode())
      .set_packet_count_for_server(constraints.default_settings().packet_count_for_server())
      .set_packet_count_for_client(constraints.default_settings().packet_count_for_client())
      .set_sysmem_token(std::move(token));

  return settings;
}

auto CreateValidInputBufferCollectionConstraints() {
  fuchsia::sysmem::BufferCollectionConstraints result;
  result.usage.cpu = fuchsia::sysmem::cpuUsageRead | fuchsia::sysmem::cpuUsageReadOften;
  result.min_buffer_count_for_camping = kInputMinBufferCountForCamping;
  return result;
}

}  // namespace

class CodecImplFailures : public gtest::RealLoopFixture {
 public:
  using StreamProcessorPtr = ::fuchsia::media::StreamProcessorPtr;

  void TearDown() override { token_request_.reset(); }

  void Create(fidl::InterfaceRequest<fuchsia::media::StreamProcessor> request) {
    fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem;
    sysmem_request_ = sysmem.NewRequest();

    codec_impl_ =
        std::make_unique<CodecImpl>(std::move(sysmem), nullptr, dispatcher(), thrd_current(),
                                    CreateDecoderParams(), std::move(request));

    auto codec_adapter = std::make_unique<FakeCodecAdapter>(codec_impl_->lock(), codec_impl_.get());
    codec_adapter_ = codec_adapter.get();
    codec_impl_->SetCoreCodecAdapter(std::move(codec_adapter));

    codec_impl_->BindAsync([this]() {
      error_handler_ran_ = true;
      codec_impl_.reset();
    });
  }

 protected:
  // Just cache this request so that we can have a valid sysmem handle
  std::optional<fidl::InterfaceRequest<fuchsia::sysmem::Allocator>> sysmem_request_;
  std::optional<fidl::InterfaceRequest<fuchsia::sysmem::BufferCollectionToken>> token_request_;

  bool error_handler_ran_ = false;
  std::unique_ptr<CodecImpl> codec_impl_;
  FakeCodecAdapter* codec_adapter_;
};

TEST_F(CodecImplFailures, InputBufferCollectionConstraintsCpuUsage) {
  StreamProcessorPtr processor;

  processor.events().OnInputConstraints = [this, &processor](auto input_constraints) {
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
    token_request_ = token.NewRequest();

    auto buffer_collection_constraints = CreateValidInputBufferCollectionConstraints();
    // Setting write usage on input buffers is invalid and will result in codec
    // failure
    buffer_collection_constraints.usage.cpu =
        fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageWriteOften;
    codec_adapter_->SetBufferCollectionConstraints(kInputPort,
                                                   std::move(buffer_collection_constraints));

    processor->SetInputBufferPartialSettings(
        CreateStreamBufferPartialSettings(1, input_constraints, std::move(token)));
  };

  Create(processor.NewRequest());

  RunLoopWithTimeoutOrUntil([this]() { return error_handler_ran_; });
  ASSERT_TRUE(error_handler_ran_);
}

TEST_F(CodecImplFailures, InputBufferCollectionConstraintsMinBufferCount) {
  StreamProcessorPtr processor;

  processor.events().OnInputConstraints = [this, &processor](auto input_constraints) {
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
    token_request_ = token.NewRequest();

    auto buffer_collection_constraints = CreateValidInputBufferCollectionConstraints();
    // No buffers required for camping would be less than the minimum for the
    // server
    buffer_collection_constraints.min_buffer_count_for_camping = 0;
    codec_adapter_->SetBufferCollectionConstraints(kInputPort,
                                                   std::move(buffer_collection_constraints));

    processor->SetInputBufferPartialSettings(
        CreateStreamBufferPartialSettings(1, input_constraints, std::move(token)));
  };

  Create(processor.NewRequest());

  RunLoopWithTimeoutOrUntil([this]() { return error_handler_ran_; });
  ASSERT_TRUE(error_handler_ran_);
}
