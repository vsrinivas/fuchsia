// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fake_codec_adapter.h>
#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/mediacodec/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/gtest/real_loop_fixture.h>

#include <gtest/gtest.h>

#include "lib/media/codec_impl/codec_impl.h"

namespace {

constexpr uint32_t kInputMinBufferCountForCamping = 3;

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

  // We can leave single_buffer_mode un-set (implies false).  The packet_count_for_server and
  // packet_count_for_client fields will be deprecated, so leave those un-set.
  settings.set_buffer_lifetime_ordinal(buffer_lifetime_ordinal)
      .set_buffer_constraints_version_ordinal(kBufferConstraintsVersionOrdinal)
      .set_sysmem_token(std::move(token));

  return settings;
}

auto CreateValidInputBufferCollectionConstraints() {
  fuchsia::sysmem::BufferCollectionConstraints result;
  result.usage.cpu = fuchsia::sysmem::cpuUsageRead | fuchsia::sysmem::cpuUsageReadOften;
  result.min_buffer_count_for_camping = kInputMinBufferCountForCamping;
  // Must specify true here, as enforced by CodecImpl.  Leaving all
  // buffer_memory_constraints fields default is fine.
  result.has_buffer_memory_constraints = true;
  return result;
}

}  // namespace

class CodecImplFailures : public gtest::RealLoopFixture {
 public:
  using StreamProcessorPtr = ::fuchsia::media::StreamProcessorPtr;

  void TearDown() override { token_request_ = nullptr; }

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
      codec_impl_ = nullptr;
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

  RunLoopUntil([this]() { return error_handler_ran_; });
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

  RunLoopUntil([this]() { return error_handler_ran_; });
  ASSERT_TRUE(error_handler_ran_);
}

class TestBufferCollection : public fuchsia::sysmem::testing::BufferCollection_TestBase {
 public:
  TestBufferCollection() : binding_(this) {}

  void Bind(fidl::InterfaceRequest<fuchsia::sysmem::BufferCollection> request) {
    binding_.Bind(std::move(request));
  }
  void NotImplemented_(const std::string& name) override {}

  void WaitForBuffersAllocated(WaitForBuffersAllocatedCallback callback) override {
    wait_callback_ = std::move(callback);
  }
  void FailAllocation() {
    WaitForBuffersAllocatedCallback callback;
    callback.swap(wait_callback_);

    callback(ZX_ERR_NOT_SUPPORTED, fuchsia::sysmem::BufferCollectionInfo_2());
  }

  bool is_waiting() { return !!wait_callback_; }

 private:
  fidl::Binding<fuchsia::sysmem::BufferCollection> binding_;
  WaitForBuffersAllocatedCallback wait_callback_;
};

class TestAllocator : public fuchsia::sysmem::testing::Allocator_TestBase {
 public:
  TestAllocator() : binding_(this) {}

  void Bind(fidl::InterfaceRequest<fuchsia::sysmem::Allocator> request) {
    binding_.Bind(std::move(request));
  }
  void BindSharedCollection(fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                            fidl::InterfaceRequest<fuchsia::sysmem::BufferCollection>
                                buffer_collection_request) override {
    collection_.Bind(std::move(buffer_collection_request));
  }

  void NotImplemented_(const std::string& name) override {
    // Unexpected.
    ZX_PANIC("NotImplemented_(): %s\n", name.c_str());
  }

  TestBufferCollection& collection() { return collection_; }

 private:
  fidl::Binding<fuchsia::sysmem::Allocator> binding_;

  TestBufferCollection collection_;
};

TEST_F(CodecImplFailures, InputBufferCollectionSysmemFailure) {
  StreamProcessorPtr processor;

  processor.events().OnInputConstraints = [this, &processor](auto input_constraints) {
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
    token_request_ = token.NewRequest();

    codec_adapter_->SetBufferCollectionConstraints(kInputPort,
                                                   CreateValidInputBufferCollectionConstraints());

    processor->SetInputBufferPartialSettings(
        CreateStreamBufferPartialSettings(1, input_constraints, std::move(token)));
  };

  Create(processor.NewRequest());

  TestAllocator allocator;
  allocator.Bind(std::move(sysmem_request_.value()));
  sysmem_request_ = nullptr;

  RunLoopUntil([&allocator]() { return allocator.collection().is_waiting(); });
  ASSERT_TRUE(error_handler_ran_ == 0);
  ASSERT_TRUE(allocator.collection().is_waiting());

  allocator.collection().FailAllocation();

  RunLoopUntil([this]() { return error_handler_ran_; });
  ASSERT_TRUE(error_handler_ran_);
}
