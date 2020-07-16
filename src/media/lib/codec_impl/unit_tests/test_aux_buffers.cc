// Copyright 2020 The Fuchsia Authors. All rights reserved.
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
#include <lib/media/codec_impl/codec_impl.h>

#include <gtest/gtest.h>

namespace sysmem = ::fuchsia::sysmem;
namespace media = ::fuchsia::media;

namespace {

constexpr uint32_t kBufferCount = 3;

constexpr sysmem::BufferCollectionConstraintsAuxBuffers kDisallowAuxBuffers{
    .need_clear_aux_buffers_for_secure = false, .allow_clear_aux_buffers_for_secure = false};
constexpr sysmem::BufferCollectionConstraintsAuxBuffers kAllowAuxBuffers{
    .need_clear_aux_buffers_for_secure = false, .allow_clear_aux_buffers_for_secure = true};
constexpr sysmem::BufferCollectionConstraintsAuxBuffers kNeedAuxBuffers{
    .need_clear_aux_buffers_for_secure = true, .allow_clear_aux_buffers_for_secure = false};

fuchsia::mediacodec::CreateDecoder_Params CreateDecoderParams() {
  fuchsia::mediacodec::CreateDecoder_Params params;

  params.mutable_input_details()->set_format_details_version_ordinal(0);
  return params;
}

media::StreamBufferPartialSettings CreateStreamBufferPartialSettings(
    uint64_t buffer_lifetime_ordinal, const media::StreamBufferConstraints& constraints,
    fidl::InterfaceHandle<sysmem::BufferCollectionToken> token) {
  constexpr uint64_t kBufferConstraintsVersionOrdinal = 1;

  media::StreamBufferPartialSettings settings;

  settings.set_buffer_lifetime_ordinal(buffer_lifetime_ordinal)
      .set_buffer_constraints_version_ordinal(kBufferConstraintsVersionOrdinal)
      .set_single_buffer_mode(constraints.default_settings().single_buffer_mode())
      .set_packet_count_for_server(constraints.default_settings().packet_count_for_server())
      .set_packet_count_for_client(constraints.default_settings().packet_count_for_client())
      .set_sysmem_token(std::move(token));

  return settings;
}

sysmem::BufferCollectionConstraints CreateValidInputBufferCollectionConstraints() {
  sysmem::BufferCollectionConstraints result;
  result.min_buffer_count_for_camping = kBufferCount;
  // Must specify true here, as enforced by CodecImpl.  Leaving all
  // buffer_memory_constraints fields default is fine.
  result.has_buffer_memory_constraints = true;
  return result;
}

sysmem::BufferCollectionInfo_2 CreateBufferCollectionInfo(bool vmos_needed = true,
                                                          bool is_secure = false) {
  sysmem::BufferCollectionInfo_2 info;

  constexpr uint32_t kBufferSize = 5000;
  info.buffer_count = kBufferCount;
  info.settings.buffer_settings.size_bytes = kBufferSize;
  info.settings.buffer_settings.is_secure = is_secure;

  for (uint32_t i = 0; i < kBufferCount; i++) {
    if (vmos_needed) {
      zx::vmo::create(kBufferSize, 0, &info.buffers[i].vmo);
    }
    info.buffers[i].vmo_usable_start = 0;
  }
  return info;
}

class AuxBufferTestCodecAdapter : public FakeCodecAdapter {
 public:
  AuxBufferTestCodecAdapter(std::mutex& lock, CodecAdapterEvents* events)
      : FakeCodecAdapter(lock, events) {}
  ~AuxBufferTestCodecAdapter() = default;

  void CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) override {
    if (port == kInputPort) {
      input_buffers_.push_back(buffer);
    }
  }
  void CoreCodecEnsureBuffersNotConfigured(CodecPort port) override {
    if (port == kInputPort) {
      input_buffers_.clear();
    }
  }

  const std::vector<const CodecBuffer*>& input_buffers() const { return input_buffers_; }

 private:
  std::vector<const CodecBuffer*> input_buffers_;
};

class TestBufferCollection : public sysmem::testing::BufferCollection_TestBase {
 public:
  TestBufferCollection() : binding_(this) {}

  // The BufferCollection_TestBase base class will call this for any protocol methods we don't
  // override.
  void NotImplemented_(const std::string&) override {}

  void Bind(fidl::InterfaceRequest<sysmem::BufferCollection> request) {
    binding_.Bind(std::move(request));
  }

  // BufferCollection Implementation
  void SetConstraints(bool has_constraints,
                      sysmem::BufferCollectionConstraints constraints) override {
    if (has_constraints) {
      buffer_collection_constraints_ = std::move(constraints);
    }
  }

  void WaitForBuffersAllocated(WaitForBuffersAllocatedCallback callback) override {
    wait_callback_ = std::move(callback);
  }

  void SetConstraintsAuxBuffers(
      sysmem::BufferCollectionConstraintsAuxBuffers constraints) override {
    aux_buffer_collection_constraints_ = std::move(constraints);
  }

  void GetAuxBuffers(GetAuxBuffersCallback callback) override {
    callback(aux_buffer_collection_info_.status, fidl::Clone(aux_buffer_collection_info_.info));
  }

  // Test hooks
  bool is_waiting() { return !!wait_callback_; }
  void set_buffer_collection_info(zx_status_t status, sysmem::BufferCollectionInfo_2 info) {
    buffer_collection_info_.status = status;
    buffer_collection_info_.info = std::move(info);
  }
  void set_aux_buffer_collection_info(zx_status_t status, sysmem::BufferCollectionInfo_2 info) {
    aux_buffer_collection_info_.status = status;
    aux_buffer_collection_info_.info = std::move(info);
  }

  void CompleteBufferCollection() {
    wait_callback_(buffer_collection_info_.status, fidl::Clone(buffer_collection_info_.info));
  }

 private:
  struct BufferCollectionInfoResult {
    zx_status_t status = ZX_ERR_INTERNAL;
    sysmem::BufferCollectionInfo_2 info;
  };

  fidl::Binding<sysmem::BufferCollection> binding_;
  WaitForBuffersAllocatedCallback wait_callback_;

  std::optional<sysmem::BufferCollectionConstraints> buffer_collection_constraints_;
  std::optional<sysmem::BufferCollectionConstraintsAuxBuffers> aux_buffer_collection_constraints_;

  BufferCollectionInfoResult buffer_collection_info_;
  BufferCollectionInfoResult aux_buffer_collection_info_;
};

class TestAllocator : public sysmem::testing::Allocator_TestBase {
 public:
  TestAllocator() : binding_(this) {}

  // The Allocator_TestBase base class will call this for any protocol methods we don't override.
  void NotImplemented_(const std::string&) override {}

  void Bind(fidl::InterfaceRequest<sysmem::Allocator> request) {
    binding_.Bind(std::move(request));
  }

  void BindSharedCollection(
      fidl::InterfaceHandle<sysmem::BufferCollectionToken> token,
      fidl::InterfaceRequest<sysmem::BufferCollection> buffer_collection_request) override {
    collection_.Bind(std::move(buffer_collection_request));
  }

  TestBufferCollection& collection() { return collection_; }

 private:
  fidl::Binding<sysmem::Allocator> binding_;

  TestBufferCollection collection_;
};

}  // namespace

class CodecImplAuxBuffers : public gtest::RealLoopFixture {
 public:
  using StreamProcessorPtr = media::StreamProcessorPtr;

  void TearDown() override { token_request_ = nullptr; }

  void Create(fidl::InterfaceRequest<media::StreamProcessor> request) {
    fidl::InterfaceHandle<sysmem::Allocator> sysmem;
    auto sysmem_request = sysmem.NewRequest();

    codec_impl_ =
        std::make_unique<CodecImpl>(std::move(sysmem), nullptr, dispatcher(), thrd_current(),
                                    CreateDecoderParams(), std::move(request));

    auto codec_adapter =
        std::make_unique<AuxBufferTestCodecAdapter>(codec_impl_->lock(), codec_impl_.get());
    codec_adapter_ = codec_adapter.get();
    codec_impl_->SetCoreCodecAdapter(std::move(codec_adapter));

    codec_impl_->BindAsync([this]() {
      error_handler_ran_ = true;
      codec_impl_ = nullptr;
    });

    allocator_.Bind(std::move(sysmem_request));
  }

  TestBufferCollection& collection() { return allocator_.collection(); }

  void RunLoopUntilWaitForBuffers() {
    RunLoopUntil([this]() { return collection().is_waiting(); });
  }

  void OnInputConstraints(const StreamProcessorPtr& processor,
                          media::StreamBufferConstraints input_constraints,
                          sysmem::BufferCollectionConstraintsAuxBuffers aux_constraints) {
    fidl::InterfaceHandle<sysmem::BufferCollectionToken> token;
    token_request_ = token.NewRequest();

    codec_adapter_->SetBufferCollectionConstraints(kInputPort,
                                                   CreateValidInputBufferCollectionConstraints());
    codec_adapter_->SetAuxBufferCollectionConstraints(kInputPort, std::move(aux_constraints));
    processor->SetInputBufferPartialSettings(
        CreateStreamBufferPartialSettings(1, input_constraints, std::move(token)));
  };

 protected:
  std::optional<fidl::InterfaceRequest<sysmem::BufferCollectionToken>> token_request_;

  bool error_handler_ran_ = false;
  std::unique_ptr<CodecImpl> codec_impl_;
  AuxBufferTestCodecAdapter* codec_adapter_;
  TestAllocator allocator_;
};

TEST_F(CodecImplAuxBuffers, InputDisallowsAuxBuffer) {
  StreamProcessorPtr processor;

  processor.events().OnInputConstraints = [this, &processor](auto input_constraints) {
    OnInputConstraints(processor, std::move(input_constraints), fidl::Clone(kDisallowAuxBuffers));
  };

  Create(processor.NewRequest());

  RunLoopUntilWaitForBuffers();
  ASSERT_FALSE(error_handler_ran_);
  ASSERT_TRUE(collection().is_waiting());

  collection().set_buffer_collection_info(ZX_OK, CreateBufferCollectionInfo());
  collection().set_aux_buffer_collection_info(ZX_OK, CreateBufferCollectionInfo());
  collection().CompleteBufferCollection();

  const auto& buffers = codec_adapter_->input_buffers();
  RunLoopUntil([&buffers]() { return buffers.size() == kBufferCount; });
  ASSERT_FALSE(error_handler_ran_);

  ASSERT_EQ(buffers.size(), kBufferCount);
  for (size_t i = 0; i < buffers.size(); i++) {
    EXPECT_NE(buffers[i]->base(), nullptr);
    EXPECT_FALSE(buffers[i]->has_aux_buffer());
  }
}

TEST_F(CodecImplAuxBuffers, InputNeedsAuxBuffer) {
  StreamProcessorPtr processor;

  processor.events().OnInputConstraints = [this, &processor](auto input_constraints) {
    OnInputConstraints(processor, std::move(input_constraints), fidl::Clone(kNeedAuxBuffers));
  };

  Create(processor.NewRequest());

  RunLoopUntilWaitForBuffers();
  ASSERT_FALSE(error_handler_ran_);
  ASSERT_TRUE(collection().is_waiting());

  collection().set_buffer_collection_info(ZX_OK, CreateBufferCollectionInfo());
  collection().set_aux_buffer_collection_info(ZX_OK, CreateBufferCollectionInfo());
  collection().CompleteBufferCollection();

  const auto& buffers = codec_adapter_->input_buffers();
  RunLoopUntil([&buffers]() { return buffers.size() == kBufferCount; });
  ASSERT_FALSE(error_handler_ran_);
  ASSERT_EQ(buffers.size(), kBufferCount);
  for (size_t i = 0; i < buffers.size(); i++) {
    EXPECT_NE(buffers[i]->base(), nullptr);
    ASSERT_TRUE(buffers[i]->has_aux_buffer());
  }
}

TEST_F(CodecImplAuxBuffers, InputNeedsAuxBufferNoneProvided) {
  StreamProcessorPtr processor;

  processor.events().OnInputConstraints = [this, &processor](auto input_constraints) {
    OnInputConstraints(processor, std::move(input_constraints), fidl::Clone(kNeedAuxBuffers));
  };

  Create(processor.NewRequest());

  RunLoopUntilWaitForBuffers();
  ASSERT_FALSE(error_handler_ran_);
  ASSERT_TRUE(collection().is_waiting());

  collection().set_buffer_collection_info(
      ZX_OK, CreateBufferCollectionInfo(/*vmos_needed=*/true, /*is_secure=*/true));
  collection().set_aux_buffer_collection_info(ZX_OK,
                                              CreateBufferCollectionInfo(/*vmos_needed=*/false));
  collection().CompleteBufferCollection();

  RunLoopUntil([this]() { return error_handler_ran_; });

  EXPECT_TRUE(error_handler_ran_);
}

TEST_F(CodecImplAuxBuffers, InputAllowsAuxBufferAndProvided) {
  StreamProcessorPtr processor;

  processor.events().OnInputConstraints = [this, &processor](auto input_constraints) {
    OnInputConstraints(processor, std::move(input_constraints), fidl::Clone(kAllowAuxBuffers));
  };

  Create(processor.NewRequest());

  RunLoopUntilWaitForBuffers();
  ASSERT_FALSE(error_handler_ran_);
  ASSERT_TRUE(collection().is_waiting());

  collection().set_buffer_collection_info(ZX_OK, CreateBufferCollectionInfo());
  collection().set_aux_buffer_collection_info(ZX_OK, CreateBufferCollectionInfo());
  collection().CompleteBufferCollection();

  const auto& buffers = codec_adapter_->input_buffers();
  RunLoopUntil([&buffers]() { return buffers.size() == kBufferCount; });

  ASSERT_EQ(buffers.size(), kBufferCount);
  for (size_t i = 0; i < buffers.size(); i++) {
    EXPECT_NE(buffers[i]->base(), nullptr);
    ASSERT_TRUE(buffers[i]->has_aux_buffer());
  }
}

TEST_F(CodecImplAuxBuffers, InputAllowsAuxBufferNoneProvided) {
  StreamProcessorPtr processor;

  processor.events().OnInputConstraints = [this, &processor](auto input_constraints) {
    OnInputConstraints(processor, std::move(input_constraints), fidl::Clone(kAllowAuxBuffers));
  };

  Create(processor.NewRequest());

  RunLoopUntilWaitForBuffers();
  ASSERT_FALSE(error_handler_ran_);
  ASSERT_TRUE(collection().is_waiting());

  collection().set_buffer_collection_info(ZX_OK, CreateBufferCollectionInfo());
  collection().set_aux_buffer_collection_info(ZX_OK,
                                              CreateBufferCollectionInfo(/*vmos_needed=*/false));
  collection().CompleteBufferCollection();

  const auto& buffers = codec_adapter_->input_buffers();
  RunLoopUntil([&buffers]() { return buffers.size() == kBufferCount; });
  ASSERT_FALSE(error_handler_ran_);

  ASSERT_EQ(buffers.size(), kBufferCount);
  for (size_t i = 0; i < buffers.size(); i++) {
    EXPECT_NE(buffers[i]->base(), nullptr);
    EXPECT_FALSE(buffers[i]->has_aux_buffer());
  }
}
