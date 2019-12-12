// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fake_codec_adapter.h>
#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/media/drm/cpp/fidl.h>
#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/zx/time.h>

#include <gtest/gtest.h>

#include "lib/media/codec_impl/codec_impl.h"

namespace {

auto CreateDecoderParams() {
  fuchsia::mediacodec::CreateDecoder_Params params;
  params.mutable_input_details()->set_format_details_version_ordinal(0).set_mime_type("video/vp9");
  return params;
}

auto CreateEncoderParams() {
  fuchsia::mediacodec::CreateEncoder_Params params;
  params.mutable_input_details()->set_format_details_version_ordinal(0).set_mime_type("audio/sbc");
  return params;
}

auto CreateDecryptorParams() {
  fuchsia::media::drm::DecryptorParams params;
  params.mutable_input_details()->set_format_details_version_ordinal(0);
  return params;
}

}  // namespace

class CodecImplLifetime : public gtest::RealLoopFixture {
 protected:
  CodecImplLifetime()
      : loop_separate_thread_(&kAsyncLoopConfigNoAttachToCurrentThread),
        admission_control_(dispatcher()) {
    // nothing else to do here
  }

  ~CodecImplLifetime() {
    // Force to teardown before admission_control_.
    RunLoopUntilIdle();
    QuitLoop();
  }

  void SetUp() override { loop_separate_thread_.StartThread("separate_thread"); }
  void TearDown() override {
    // Force any failure during ~CodecImpl to have more obvious stack.
    codec_impl_ = nullptr;
  }

  void Create(bool bind = true, bool delete_async = false,
              CodecImpl::StreamProcessorParams params = CreateDecoderParams()) {
    // Just hold onto the server end and never connect it to anything, for now.
    sysmem_request_ = sysmem_client_.NewRequest();
    codec_request_ = codec_client_handle_.NewRequest();
    ZX_DEBUG_ASSERT(!delete_async || bind);
    std::unique_ptr<CodecImpl> codec_impl;
    admission_control_.TryAddCodec(
        true, [this, bind, delete_async, params = std::move(params),
               &codec_impl](std::unique_ptr<CodecAdmission> codec_admission) mutable {
          codec_impl = std::make_unique<CodecImpl>(
              std::move(sysmem_client_), std::move(codec_admission), dispatcher(), thrd_current(),
              std::move(params), std::move(codec_request_));
          auto fake_codec_adapter =
              std::make_unique<FakeCodecAdapter>(codec_impl->lock(), codec_impl.get());
          fake_codec_adapter_ = fake_codec_adapter.get();
          codec_impl->SetCoreCodecAdapter(std::move(fake_codec_adapter));
          if (!bind) {
            return;
          }
          codec_impl->BindAsync([this, delete_async] {
            if (!delete_async) {
              codec_impl_ = nullptr;
            } else {
              zx_status_t status = async::PostTask(dispatcher(), [this] { codec_impl_ = nullptr; });
              ZX_DEBUG_ASSERT(status == ZX_OK);
            }
            error_handler_ran_ = true;
          });
        });
    RunLoopUntil([&codec_impl] { return !!codec_impl; });
    ZX_DEBUG_ASSERT(codec_impl);
    codec_impl_ = std::move(codec_impl);
  }

  void StartSyncChain() {
    if (codec_client_ptr_) {
      codec_client_ptr_->Sync([this] {
        ++sync_completion_count_;
        StartSyncChain();
      });
    }
  }

  void PostToSeparateThread(fit::closure to_run) {
    zx_status_t status = async::PostTask(loop_separate_thread_.dispatcher(), std::move(to_run));
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }

  async::Loop loop_separate_thread_;
  CodecAdmissionControl admission_control_;

  // The server end isn't connected to anything, for now.
  fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem_client_;
  fidl::InterfaceRequest<fuchsia::sysmem::Allocator> sysmem_request_;

  // Tests that don't intend to process any received messages use
  // InterfaceHandle while those that do intend to process received messages
  // use InterfacePtr.
  fidl::InterfaceHandle<fuchsia::media::StreamProcessor> codec_client_handle_;
  fuchsia::media::StreamProcessorPtr codec_client_ptr_;

  fidl::InterfaceRequest<fuchsia::media::StreamProcessor> codec_request_;

  std::unique_ptr<CodecImpl> codec_impl_;
  FakeCodecAdapter* fake_codec_adapter_;
  bool error_handler_ran_ = false;
  uint64_t sync_completion_count_ = 0;
};

TEST_F(CodecImplLifetime, CreateDelete) {
  Create(false);
  codec_impl_ = nullptr;
  RunLoopUntilIdle();
  EXPECT_FALSE(error_handler_ran_);
}

TEST_F(CodecImplLifetime, CreateBindDelete) {
  Create();
  codec_impl_ = nullptr;
  RunLoopUntilIdle();
  EXPECT_FALSE(error_handler_ran_);
}

TEST_F(CodecImplLifetime, CreateBindChannelClose) {
  Create();
  // Close the client end of the StreamProcessor channel.
  codec_client_handle_.TakeChannel().reset();
  RunLoopUntil([this] { return error_handler_ran_; });
  EXPECT_TRUE(error_handler_ran_);
}

TEST_F(CodecImplLifetime, CreateBindChannelCloseDeleteAsync) {
  Create(true, true);
  // Close the client end of the StreamProcessor channel.
  codec_client_handle_.TakeChannel().reset();
  RunLoopUntil([this] { return error_handler_ran_; });
  // This doesn't imply that !codec_impl_ yet, because deletion
  // is happening async.
  EXPECT_TRUE(error_handler_ran_);
  RunLoopUntil([this] { return !codec_impl_; });
  EXPECT_TRUE(!codec_impl_);
}

TEST_F(CodecImplLifetime, CreateBindChannelCloseDeleteAsyncWithOngoingSyncs) {
  // More than one thread is involved, so do this several times in case it helps catch something
  // bad that doesn't always happen.
  constexpr uint64_t kIterCount = 20;
  for (uint64_t iter = 0; iter < kIterCount; ++iter) {
    Create(true, true);

    codec_client_ptr_.Bind(codec_client_handle_.TakeChannel());
    constexpr uint32_t kInFlightSyncTarget = 5;
    for (uint32_t i = 0; i < kInFlightSyncTarget; ++i) {
      // Each started chain kicks another Sync() on each completion, any time
      // RunLoopUntil() is running.
      StartSyncChain();
    }

    // Make sure the sync chains will re-trigger new syncs while RunLoopUntil() is
    // running.
    RunLoopUntil([this] { return sync_completion_count_ >= kInFlightSyncTarget * 2; });

    // Trigger an error as if the FakeCodecAdapter had triggered it, with a slight
    // delay before the trigger so we get some coverage of syncs happening
    // continuously while the failure handling happens.
    PostToSeparateThread([this] {
      zx::nanosleep(zx::deadline_after(zx::msec(20)));
      static_cast<CodecAdapterEvents*>(codec_impl_.get())
          ->onCoreCodecFailCodec(
              "CreateBindChannelCloseDeleteAsyncWithOngoingSyncs triggering failure");
    });

    RunLoopUntil([this] { return !codec_impl_; });
    EXPECT_TRUE(!codec_impl_);
  }
}

TEST_F(CodecImplLifetime, CreateBindDeleteEncoder) {
  Create(true, false, CreateEncoderParams());
  codec_impl_ = nullptr;
  RunLoopUntilIdle();
  EXPECT_FALSE(error_handler_ran_);
}

TEST_F(CodecImplLifetime, CreateBindDeleteDecryptor) {
  Create(true, false, CreateDecryptorParams());
  codec_impl_ = nullptr;
  RunLoopUntilIdle();
  EXPECT_FALSE(error_handler_ran_);
}
