// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/camera/cpp/fidl.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/gtest/test_loop_fixture.h>

#include <ddktl/protocol/sysmem.h>

#include "src/camera/drivers/controller/controller-device.h"
#include "src/camera/drivers/controller/controller-protocol.h"
#include "src/camera/drivers/controller/test/constants.h"
#include "src/camera/drivers/controller/test/fake_sysmem.h"

namespace camera {
namespace {

class ControllerDeviceTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    ddk_ = std::make_unique<fake_ddk::Bind>();
    static constexpr const uint32_t kNumSubDevices = 1;
    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[kNumSubDevices],
                                                  kNumSubDevices);
    protocols[0] = fake_sysmem_.ProtocolEntry();
    ddk_->SetProtocols(std::move(protocols));
    zx::event event;
    ASSERT_EQ(ZX_OK, zx::event::create(0, &event));

    controller_device_ = std::make_unique<ControllerDevice>(
        fake_ddk::kFakeParent, fake_ddk::kFakeParent, fake_ddk::kFakeParent, fake_ddk::kFakeParent,
        fake_ddk::kFakeParent, std::move(event));
  }

  void TearDown() override {
    if (controller_device_) {
      controller_device_->DdkAsyncRemove();
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
    // TODO(fxbug.dev/38554): allow unidirectional message processing
    // Currently, running a loop associated with fidl::InterfacePtr handles both inbound and
    // outbound messages. Depending on how quickly the server handles such requests, the
    // channel may or may not be closed by the time a single call to RunUntilIdle returns.
    zx_status_t status = channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr);
    if (status != ZX_OK) {
      EXPECT_EQ(status, ZX_ERR_BAD_HANDLE);
    }
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
    WaitForChannelClosure(ptr.channel());
    RunLoopUntilIdle();
    if (epitaph_received) {  // Epitaphs are not guaranteed to be returned.
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
  FakeSysmem fake_sysmem_;
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
  camera_protocol_->GetChannel(controller_protocol_.NewRequest().TakeChannel());
  RunLoopUntilIdle();
  WaitForChannelClosure(controller_protocol_.channel());
  WaitForInterfaceClosure(camera_protocol_, ZX_ERR_PEER_CLOSED);
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
  RunLoopUntilIdle();
  fuchsia::camera2::hal::ControllerPtr other_controller_protocol;
  camera_protocol_->GetChannel2(other_controller_protocol.NewRequest().TakeChannel());
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
TEST_F(ControllerDeviceTest, GetNextConfig) {
  ASSERT_NO_FATAL_FAILURE(BindControllerProtocol());
  uint32_t number_of_configs = 0;
  constexpr uint32_t kNumConfigs = 3;
  bool config_populated = false;

  while (number_of_configs != kNumConfigs) {
    controller_protocol_->GetNextConfig(
        [&](std::unique_ptr<fuchsia::camera2::hal::Config> config, zx_status_t status) {
          switch (number_of_configs) {
            case SherlockConfigs::MONITORING: {
              // Config 0 (monitoring)
              ASSERT_NE(config, nullptr);
              EXPECT_EQ(config->stream_configs.at(0).properties.stream_type(),
                        fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                            fuchsia::camera2::CameraStreamType::MACHINE_LEARNING);
              EXPECT_EQ(config->stream_configs.at(1).properties.stream_type(),
                        fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION |
                            fuchsia::camera2::CameraStreamType::MACHINE_LEARNING);
              EXPECT_EQ(config->stream_configs.at(2).properties.stream_type(),
                        fuchsia::camera2::CameraStreamType::MONITORING);
              break;
            }
            case SherlockConfigs::VIDEO: {
              // Config 1 (video conferencing)
              ASSERT_NE(config, nullptr);
              EXPECT_EQ(config->stream_configs.at(0).properties.stream_type(),
                        fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE |
                            fuchsia::camera2::CameraStreamType::MACHINE_LEARNING |
                            fuchsia::camera2::CameraStreamType::FULL_RESOLUTION);
              EXPECT_EQ(config->stream_configs.at(1).properties.stream_type(),
                        fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE);
              break;
            }
            case SherlockConfigs::VIDEO_EXTENDED_FOV: {
              // Config 2 (video conferencing with extended FOV)
              ASSERT_NE(config, nullptr);
              EXPECT_EQ(config->stream_configs.at(0).properties.stream_type(),
                        fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE |
                            fuchsia::camera2::CameraStreamType::MACHINE_LEARNING |
                            fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                            fuchsia::camera2::CameraStreamType::EXTENDED_FOV);
              EXPECT_EQ(config->stream_configs.at(1).properties.stream_type(),
                        fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE |
                            fuchsia::camera2::CameraStreamType::EXTENDED_FOV);
              break;
            }
            default: {
              EXPECT_EQ(config, nullptr);
              EXPECT_EQ(status, ZX_ERR_STOP);
              break;
            }
          }

          config_populated = true;
          number_of_configs++;
        });

    while (!config_populated) {
      RunLoopUntilIdle();
    }
    config_populated = false;
  }
}

TEST_F(ControllerDeviceTest, CreateStreamInvalidArgs) {
  ASSERT_NO_FATAL_FAILURE(BindControllerProtocol());
  fuchsia::camera2::StreamPtr stream;
  std::unique_ptr<fuchsia::camera2::hal::Config> camera_config;
  bool configs_populated = false;
  controller_protocol_->GetNextConfig(
      [&](std::unique_ptr<fuchsia::camera2::hal::Config> config, zx_status_t status) {
        ASSERT_NE(config, nullptr);
        ASSERT_EQ(status, ZX_OK);
        configs_populated = true;
        camera_config = std::move(config);
      });
  while (!configs_populated) {
    RunLoopUntilIdle();
  }

  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection;
  buffer_collection.buffer_count = 0;

  // Invalid config index.
  constexpr uint32_t kInvalidConfigIndex = 10;
  controller_protocol_->CreateStream(kInvalidConfigIndex, 0, 0, std::move(buffer_collection),
                                     stream.NewRequest());
  WaitForInterfaceClosure(stream, ZX_ERR_INVALID_ARGS);

  // Invalid stream index.
  controller_protocol_->CreateStream(0, camera_config->stream_configs.size(), 0,
                                     std::move(buffer_collection), stream.NewRequest());
  WaitForInterfaceClosure(stream, ZX_ERR_INVALID_ARGS);

  // Invalid format index.
  controller_protocol_->CreateStream(0, 0, camera_config->stream_configs[0].image_formats.size(),
                                     std::move(buffer_collection), stream.NewRequest());
  WaitForInterfaceClosure(stream, ZX_ERR_INVALID_ARGS);

  // Not enough buffers.
  controller_protocol_->CreateStream(0, 0, 0, std::move(buffer_collection), stream.NewRequest());
  WaitForInterfaceClosure(stream, ZX_ERR_INVALID_ARGS);
}

}  // namespace
}  // namespace camera
