// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include "src/camera/bin/device/device_impl.h"
#include "src/camera/bin/device/stream_impl.h"
#include "src/camera/bin/device/test/fake_controller.h"
#include "src/camera/bin/device/util.h"
#include "src/camera/lib/fake_legacy_stream/fake_legacy_stream.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace camera {

// No-op function.
static void nop() {}

class DeviceTest : public gtest::RealLoopFixture {
 protected:
  DeviceTest() : context_(sys::ComponentContext::Create()) {}

  void SetUp() override {
    context_->svc()->Connect(allocator_.NewRequest());
    allocator_.set_error_handler(MakeErrorHandler("Sysmem Allocator"));

    fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller;
    auto controller_result = FakeController::Create(controller.NewRequest());
    ASSERT_TRUE(controller_result.is_ok());
    controller_ = controller_result.take_value();

    fidl::InterfaceHandle<fuchsia::sysmem::Allocator> allocator;
    context_->svc()->Connect(allocator.NewRequest());

    auto device_result = DeviceImpl::Create(std::move(controller), std::move(allocator));
    ASSERT_TRUE(device_result.is_ok());
    device_ = device_result.take_value();
  }

  void TearDown() override {
    device_ = nullptr;
    controller_ = nullptr;
    allocator_ = nullptr;
    RunLoopUntilIdle();
  }

  static fit::function<void(zx_status_t status)> MakeErrorHandler(std::string server) {
    return [server](zx_status_t status) {
      ADD_FAILURE() << server << " server disconnected - " << status;
    };
  }

  std::unique_ptr<sys::ComponentContext> context_;
  std::unique_ptr<DeviceImpl> device_;
  std::unique_ptr<FakeController> controller_;
  fuchsia::sysmem::AllocatorPtr allocator_;
};

TEST_F(DeviceTest, CreateStreamNullConnection) { StreamImpl stream(nullptr, nullptr, 0, nop); }

TEST_F(DeviceTest, CreateStreamFakeLegacyStream) {
  fidl::InterfaceHandle<fuchsia::camera2::Stream> handle;
  auto result = FakeLegacyStream::Create(handle.NewRequest());
  ASSERT_TRUE(result.is_ok());
  { StreamImpl stream(std::move(handle), nullptr, 0, nop); }
}

TEST_F(DeviceTest, CreateStreamNoClientBuffers) {
  fuchsia::camera3::DevicePtr device;
  device_->GetHandler()(device.NewRequest());
  device.set_error_handler(MakeErrorHandler("Device"));
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
  allocator_->AllocateSharedCollection(token.NewRequest());
  fuchsia::camera3::StreamPtr stream;
  device->ConnectToStream(0, std::move(token), stream.NewRequest());
  bool stream_errored = false;
  zx_status_t stream_status = ZX_OK;
  stream.set_error_handler([&](zx_status_t status) {
    stream_status = status;
    stream_errored = true;
  });
  fuchsia::camera3::StreamPtr stream2;
  stream->Rebind(stream2.NewRequest());
  bool stream2_errored = false;
  zx_status_t stream2_status = ZX_OK;
  stream2.set_error_handler([&](zx_status_t status) {
    stream2_status = status;
    stream2_errored = true;
  });
  while (!HasFailure() && !(stream_errored && stream2_errored)) {
    RunLoopUntilIdle();
  }
  EXPECT_EQ(stream_status, ZX_ERR_NOT_SUPPORTED);
  EXPECT_EQ(stream2_status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(DeviceTest, ConvertConfig) {
  auto configs = FakeController::GetDefaultConfigs();
  ASSERT_FALSE(configs.empty());
  auto& a = configs[0];
  ASSERT_FALSE(a.stream_configs.empty());
  ASSERT_FALSE(a.stream_configs[0].image_formats.empty());
  auto result = Convert(a);
  ASSERT_TRUE(result.is_ok());
  auto b = result.take_value();
  EXPECT_EQ(a.stream_configs.size(), b.streams.size());
  ASSERT_FALSE(b.streams[0].supported_resolutions.empty());
  EXPECT_EQ(a.stream_configs[0].image_formats[0].bytes_per_row,
            b.streams[0].supported_resolutions[0].bytes_per_row);
}

TEST_F(DeviceTest, GetFrames) {
  fidl::InterfaceHandle<fuchsia::camera2::Stream> handle;
  auto result = FakeLegacyStream::Create(handle.NewRequest());
  ASSERT_TRUE(result.is_ok());
  auto legacy_stream_fake = result.take_value();
  fuchsia::camera3::StreamPtr stream;
  stream.set_error_handler(MakeErrorHandler("Stream"));
  constexpr uint32_t kMaxCampingBuffers = 2;
  constexpr uint32_t kBufferId1 = 42;
  constexpr uint32_t kBufferId2 = 17;
  auto stream_impl =
      std::make_unique<StreamImpl>(std::move(handle), stream.NewRequest(), kMaxCampingBuffers, nop);
  bool frame1_received = false;
  bool frame2_received = false;
  auto callback2 = [&](fuchsia::camera3::FrameInfo info) {
    ASSERT_EQ(info.buffer_index, kBufferId2);
    frame2_received = true;
  };
  auto callback1 = [&](fuchsia::camera3::FrameInfo info) {
    ASSERT_EQ(info.buffer_index, kBufferId1);
    frame1_received = true;
    stream->GetNextFrame(std::move(callback2));
  };
  stream->GetNextFrame(std::move(callback1));
  fuchsia::camera2::FrameAvailableInfo frame1_info;
  frame1_info.frame_status = fuchsia::camera2::FrameStatus::OK;
  frame1_info.buffer_id = kBufferId1;
  frame1_info.metadata.set_timestamp(0);
  ASSERT_EQ(legacy_stream_fake->SendFrameAvailable(std::move(frame1_info)), ZX_OK);
  fuchsia::camera2::FrameAvailableInfo frame2_info;
  frame2_info.frame_status = fuchsia::camera2::FrameStatus::OK;
  frame2_info.buffer_id = kBufferId2;
  frame2_info.metadata.set_timestamp(0);
  ASSERT_EQ(legacy_stream_fake->SendFrameAvailable(std::move(frame2_info)), ZX_OK);
  while (!HasFailure() && (!frame1_received || !frame2_received)) {
    RunLoopUntilIdle();
  }
  auto client_result = legacy_stream_fake->StreamClientStatus();
  EXPECT_TRUE(client_result.is_ok()) << client_result.error();
  stream = nullptr;
  stream_impl = nullptr;
}

TEST_F(DeviceTest, GetFramesInvalidCall) {
  fidl::InterfaceHandle<fuchsia::camera2::Stream> handle;
  auto result = FakeLegacyStream::Create(handle.NewRequest());
  ASSERT_TRUE(result.is_ok());
  auto legacy_stream_fake = result.take_value();
  bool stream_errored = false;
  fuchsia::camera3::StreamPtr stream;
  stream.set_error_handler([&](zx_status_t status) {
    EXPECT_EQ(status, ZX_ERR_BAD_STATE);
    stream_errored = true;
  });
  auto stream_impl = std::make_unique<StreamImpl>(std::move(handle), stream.NewRequest(), 0, nop);
  stream->GetNextFrame([](fuchsia::camera3::FrameInfo info) {});
  stream->GetNextFrame([](fuchsia::camera3::FrameInfo info) {});
  while (!HasFailure() && !stream_errored) {
    RunLoopUntilIdle();
  }
}

}  // namespace camera
