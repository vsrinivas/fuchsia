// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/function.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/zx/clock.h>

#include <fbl/auto_call.h>
#include <fbl/function.h>

#include "../virtual_camera2_control.h"
#include "gtest/gtest.h"
#include "stream_tester.h"

namespace camera {
namespace {

// Test the controller part of the virtual camera
class CameraHalTest : public testing::Test {
 public:
  CameraHalTest()
      : loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        virtual_camera2_(camera_client_.NewRequest(), loop_.dispatcher(), [] {}) {
    zx_status_t status = loop_.StartThread("camera-controller-loop");
    EXPECT_EQ(status, ZX_OK) << "Failed to StartThread for tests. status: " << status;
  }

  ~CameraHalTest() override { loop_.Shutdown(); }

  void TestGetConfigs() {
    zx_status_t out_status;
    fidl::VectorPtr<fuchsia::camera2::hal::Config> out_configs;
    zx_status_t fidl_status = camera_client_->GetConfigs(&out_configs, &out_status);
    ASSERT_EQ(fidl_status, ZX_OK) << "Couldn't get Camera Configs. fidl status: " << fidl_status;
    ASSERT_EQ(out_status, ZX_OK) << "Couldn't get Camera Configs. status: " << out_status;
    ASSERT_TRUE(out_configs) << "Couldn't get Camera Configs. No Configs.";
    // Test more about the configs
    configs_ = std::move(out_configs.value());
    // Assert that there is at least one config
    ASSERT_TRUE(configs_.size());
    // Assert that each config has at least one stream config:
    for (auto& config : configs_) {
      ASSERT_TRUE(config.stream_configs.size());
      for (auto& stream : config.stream_configs) {
        // Assert that each stream config has at least one image format:
        ASSERT_TRUE(stream.image_formats.size());
      }
    }
  }

  void TestGetDeviceInfo() {
    fuchsia::camera2::DeviceInfo device_info;
    zx_status_t fidl_status = camera_client_->GetDeviceInfo(&device_info);
    ASSERT_EQ(fidl_status, ZX_OK) << "Couldn't get device info. fidl status: " << fidl_status;
    EXPECT_EQ(kVirtualCameraVendorName, device_info.vendor_name());
    EXPECT_EQ(kVirtualCameraProductName, device_info.product_name());
    EXPECT_EQ(fuchsia::camera2::DeviceType::VIRTUAL, device_info.type());
  }

  void TestCreateStream0() {
    // Start by testing the first config, first stream, first format:
    // TODO(garratt): make actual buffer collection
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection;
    fuchsia::camera2::StreamPtr stream;
    zx_status_t fidl_status = camera_client_->CreateStream(0, 0, 0, std::move(buffer_collection),
                                                           stream.NewRequest(loop_.dispatcher()));
    ASSERT_EQ(fidl_status, ZX_OK) << "Couldn't create stream. fidl status: " << fidl_status;
    // Assert that the channel is open:
    ASSERT_TRUE(stream.is_bound());
    ASSERT_TRUE(camera_client_.is_bound());
    camera::StreamTester stream_tester(stream.Unbind().TakeChannel());
    stream_tester.TestGetFrames();
  }

 private:
  async::Loop loop_;
  std::vector<fuchsia::camera2::hal::Config> configs_;
  fuchsia::camera2::hal::ControllerSyncPtr camera_client_;
  VirtualCamera2ControllerImpl virtual_camera2_;
};

TEST_F(CameraHalTest, GetStartupInfo) {
  TestGetConfigs();
  TestGetDeviceInfo();
}

TEST_F(CameraHalTest, ConnectToStream0) { TestCreateStream0(); }

}  // namespace
}  // namespace camera
