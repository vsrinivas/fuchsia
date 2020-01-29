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
#include "src/camera/lib/fake_stream/fake_stream.h"
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

TEST_F(DeviceTest, CreateStreamNullConnection) { StreamImpl stream(nullptr, nullptr, nop); }

TEST_F(DeviceTest, CreateStreamFakeLegacyStream) {
  fidl::InterfaceHandle<fuchsia::camera2::Stream> handle;
  auto result = FakeStream::Create(handle.NewRequest());
  ASSERT_TRUE(result.is_ok());
  { StreamImpl stream(std::move(handle), nullptr, nop); }
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

}  // namespace camera
