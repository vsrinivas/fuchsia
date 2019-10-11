// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../controller-device.h"

#include <fuchsia/hardware/camera/cpp/fidl.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>

#include <fbl/auto_call.h>

#include "../controller-protocol.h"

namespace camera {
namespace {

class ControllerDeviceTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    ddk_ = std::make_unique<fake_ddk::Bind>();
    controller_device_ = std::make_unique<ControllerDevice>(
        fake_ddk::kFakeParent, fake_ddk::kFakeParent, fake_ddk::kFakeParent);
  }

  void TearDown() override {
    if (controller_device_) {
      ASSERT_EQ(controller_device_->DdkRemoveDeprecated(), ZX_OK);
    }
    ASSERT_EQ(ddk_->WaitUntilRemove(), ZX_OK);
    ASSERT_TRUE(ddk_->Ok());
    ddk_ = nullptr;

    controller_protocol_ = nullptr;
    controller_device_ = nullptr;
  }

  static void FailErrorHandler(zx_status_t status) {
    ADD_FAILURE() << "Channel Failure: " << status;
  }

  static void WaitForChannelClosure(const zx::channel& channel) {
    ASSERT_EQ(channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr), ZX_OK);
  }

  template <class T>
  void WaitForInterfaceClosure(fidl::InterfacePtr<T>& ptr, zx_status_t expected_epitaph) {
    bool epitaph_received = false;
    zx_status_t epitaph_status = ZX_OK;
    ptr.set_error_handler([&](zx_status_t status) {
      ASSERT_FALSE(epitaph_received) << "We should only get one epitaph!";
      epitaph_received = true;
      epitaph_status = status;
    });
    ASSERT_EQ(ptr.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr), ZX_OK);
    RunLoopUntilIdle();
    EXPECT_TRUE(epitaph_received);
    if (epitaph_received) {
      EXPECT_EQ(epitaph_status, expected_epitaph);
    }
  }

  void BindControllerProtocol() {
    ASSERT_EQ(controller_device_->DdkAdd("test-camera-controller"), ZX_OK);
    ASSERT_EQ(controller_device_->StartThread(), ZX_OK);
    ASSERT_EQ(camera_protocol_.Bind(std::move(ddk_->FidlClient())), ZX_OK);
    camera_protocol_.set_error_handler(FailErrorHandler);
    camera_protocol_->GetChannel2(controller_protocol_.NewRequest().TakeChannel());
    controller_protocol_.set_error_handler(FailErrorHandler);
    RunLoopUntilIdle();
  }

  std::unique_ptr<fake_ddk::Bind> ddk_;
  std::unique_ptr<ControllerDevice> controller_device_;
  fuchsia::hardware::camera::DevicePtr camera_protocol_;
  fuchsia::camera2::hal::ControllerPtr controller_protocol_;
};

// Verifies controller can start up and shut down.
TEST_F(ControllerDeviceTest, DdkLifecycle) {
  EXPECT_EQ(controller_device_->DdkAdd("test-camera-controller"), ZX_OK);
  EXPECT_EQ(controller_device_->StartThread(), ZX_OK);
  controller_device_->DdkAsyncRemove();
  EXPECT_TRUE(ddk_->Ok());
}

// Verifies GetChannel is not supported.
TEST_F(ControllerDeviceTest, GetChannel) {
  EXPECT_EQ(controller_device_->DdkAdd("test-camera-controller"), ZX_OK);
  EXPECT_EQ(controller_device_->StartThread(), ZX_OK);
  ASSERT_EQ(camera_protocol_.Bind(std::move(ddk_->FidlClient())), ZX_OK);
  camera_protocol_.set_error_handler(FailErrorHandler);
  camera_protocol_->GetChannel(controller_protocol_.NewRequest().TakeChannel());
  RunLoopUntilIdle();
  WaitForChannelClosure(controller_protocol_.channel());
}

// Verifies that GetChannel2 works correctly.
TEST_F(ControllerDeviceTest, GetChannel2) {
  EXPECT_EQ(controller_device_->DdkAdd("test-camera-controller"), ZX_OK);
  EXPECT_EQ(controller_device_->StartThread(), ZX_OK);
  ASSERT_EQ(camera_protocol_.Bind(std::move(ddk_->FidlClient())), ZX_OK);
  camera_protocol_->GetChannel2(controller_protocol_.NewRequest().TakeChannel());
  camera_protocol_.set_error_handler(FailErrorHandler);
  RunLoopUntilIdle();
}

// Verifies that GetChannel2 can only have one binding.
TEST_F(ControllerDeviceTest, GetChannel2InvokeTwice) {
  EXPECT_EQ(controller_device_->DdkAdd("test-camera-controller"), ZX_OK);
  EXPECT_EQ(controller_device_->StartThread(), ZX_OK);
  ASSERT_EQ(camera_protocol_.Bind(std::move(ddk_->FidlClient())), ZX_OK);
  camera_protocol_->GetChannel2(controller_protocol_.NewRequest().TakeChannel());
  camera_protocol_.set_error_handler(FailErrorHandler);
  RunLoopUntilIdle();
  fuchsia::camera2::hal::ControllerPtr other_controller_protocol;
  camera_protocol_->GetChannel2(other_controller_protocol.NewRequest().TakeChannel());
  camera_protocol_.set_error_handler(FailErrorHandler);
  RunLoopUntilIdle();
  WaitForChannelClosure(other_controller_protocol.channel());
}

// Verifies sanity of returned device info.
TEST_F(ControllerDeviceTest, GetDeviceInfo) {
  ASSERT_NO_FATAL_FAILURE(BindControllerProtocol());
  controller_protocol_->GetDeviceInfo([](fuchsia::camera2::DeviceInfo device_info) {
    EXPECT_EQ(kCameraVendorName, device_info.vendor_name());
    EXPECT_EQ(kCameraProductName, device_info.product_name());
    EXPECT_EQ(fuchsia::camera2::DeviceType::BUILTIN, device_info.type());
  });
  RunLoopUntilIdle();
}

// Verifies sanity of returned configs.
TEST_F(ControllerDeviceTest, GetConfigs) {
  ASSERT_NO_FATAL_FAILURE(BindControllerProtocol());
  bool configs_populated = false;
  controller_protocol_->GetConfigs(
      [&](fidl::VectorPtr<fuchsia::camera2::hal::Config> configs, zx_status_t status) {
        ASSERT_EQ(status, ZX_OK);
        EXPECT_TRUE(configs.has_value());
        EXPECT_GT(configs->size(), 0u);
        // Config 0 (debug)
        EXPECT_EQ(configs->at(0).stream_configs.at(0).properties.stream_type(),
                  fuchsia::camera2::CameraStreamType::FULL_RESOLUTION);

        // Config 1 (monitoring)
        EXPECT_EQ(configs->at(1).stream_configs.at(0).properties.stream_type(),
                  fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                      fuchsia::camera2::CameraStreamType::MACHINE_LEARNING);
        EXPECT_EQ(configs->at(1).stream_configs.at(1).properties.stream_type(),
                  fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION |
                      fuchsia::camera2::CameraStreamType::MACHINE_LEARNING);
        EXPECT_EQ(configs->at(1).stream_configs.at(2).properties.stream_type(),
                  fuchsia::camera2::CameraStreamType::MONITORING);
        configs_populated = true;
      });
  while (!configs_populated) {
    RunLoopUntilIdle();
  }
}

TEST_F(ControllerDeviceTest, CreateStreamInvalidArgs) {
  ASSERT_NO_FATAL_FAILURE(BindControllerProtocol());
  fuchsia::camera2::StreamPtr stream;

  std::vector<fuchsia::camera2::hal::Config> configs;
  bool configs_populated = false;
  controller_protocol_->GetConfigs(
      [&](fidl::VectorPtr<fuchsia::camera2::hal::Config> configs_ptr, zx_status_t status) {
        ASSERT_EQ(status, ZX_OK);
        ASSERT_TRUE(configs_ptr.has_value());
        configs = std::move(configs_ptr).value();
        configs_populated = true;
      });
  while (!configs_populated) {
    RunLoopUntilIdle();
  }
  ASSERT_GT(configs.size(), 0u);
  ASSERT_GT(configs[0].stream_configs.size(), 0u);
  ASSERT_GT(configs[0].stream_configs[0].image_formats.size(), 0u);

  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection;
  buffer_collection.buffer_count = 0;

  // Invalid config index.
  controller_protocol_->CreateStream(configs.size(), 0, 0, std::move(buffer_collection),
                                     stream.NewRequest());
  WaitForInterfaceClosure(stream, ZX_ERR_INVALID_ARGS);

  // Invalid stream index.
  controller_protocol_->CreateStream(0, configs[0].stream_configs.size(), 0,
                                     std::move(buffer_collection), stream.NewRequest());
  WaitForInterfaceClosure(stream, ZX_ERR_INVALID_ARGS);

  // Invalid format index.
  controller_protocol_->CreateStream(0, 0, configs[0].stream_configs[0].image_formats.size(),
                                     std::move(buffer_collection), stream.NewRequest());
  WaitForInterfaceClosure(stream, ZX_ERR_INVALID_ARGS);

  // Not enough buffers.
  controller_protocol_->CreateStream(0, 0, 0, std::move(buffer_collection), stream.NewRequest());
  WaitForInterfaceClosure(stream, ZX_ERR_INVALID_ARGS);
}

}  // namespace
}  // namespace camera
