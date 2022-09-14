// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/virtual_camera/virtual_camera_agent.h"

#include <fuchsia/camera/test/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/camera/bin/virtual_camera/stream_storage.h"
#include "src/camera/lib/stream_utils/stream_constraints.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace {

class FakeDeviceWatcherTester : public fuchsia::camera::test::DeviceWatcherTester {
 public:
  MOCK_METHOD(void, InjectDevice, (fidl::InterfaceHandle<fuchsia::hardware::camera::Device>),
              (override));

  FakeDeviceWatcherTester() {
    ON_CALL(*this, InjectDevice)
        .WillByDefault([this](fidl::InterfaceHandle<fuchsia::hardware::camera::Device> handle) {
          device_ptr_.Bind(std::move(handle));
        });
  }

  fidl::InterfaceRequestHandler<fuchsia::camera::test::DeviceWatcherTester> GetInterfaceHandler() {
    return bindings_.GetHandler(this);
  }
  fidl::InterfacePtr<fuchsia::camera2::hal::Controller> GetHalController() {
    if (!device_ptr_) {
      return nullptr;
    }

    fidl::InterfacePtr<fuchsia::camera2::hal::Controller> controller;
    device_ptr_->GetChannel2(controller.NewRequest());
    return controller;
  }

 private:
  fidl::BindingSet<fuchsia::camera::test::DeviceWatcherTester> bindings_;
  fidl::InterfacePtr<fuchsia::hardware::camera::Device> device_ptr_;
};

class VirtualCameraAgentTest : public gtest::TestLoopFixture {
 protected:
  VirtualCameraAgentTest()
      : provider_(), stream_storage_(), agent_under_test_(provider_.context(), stream_storage_) {}

  void SetUp() override {
    gtest::TestLoopFixture::SetUp();

    // Create a |DeviceWatchterTester| and publish it.
    provider_.service_directory_provider()->AddService<fuchsia::camera::test::DeviceWatcherTester>(
        device_watcher_tester_.GetInterfaceHandler());
  }

  sys::testing::ComponentContextProvider provider_;
  camera::StreamStorage stream_storage_;
  camera::VirtualCameraAgent agent_under_test_;
  FakeDeviceWatcherTester device_watcher_tester_;
};

TEST_F(VirtualCameraAgentTest, TestAddToDeviceWatcher) {
  // |InjectDevice| should be called once.
  EXPECT_CALL(device_watcher_tester_, InjectDevice(::testing::_)).Times(1);

  // Call |AddToDeviceWatcher|. The response should not be an error.
  agent_under_test_.AddToDeviceWatcher([](auto result) { EXPECT_TRUE(result.is_response()); });

  // Run loop until call to |InjectDevice| completes.
  RunLoopUntilIdle();
}

TEST_F(VirtualCameraAgentTest, TestAddToDeviceWatcher_CalledTwice) {
  // |InjectDevice| should be called once. The second call should never happen
  // as it should fail with an error.
  EXPECT_CALL(device_watcher_tester_, InjectDevice(::testing::_)).Times(1);

  // Call |AddToDeviceWatcher| twice. The first call should succeed. The second
  // call should fail with ALREADY_ADDED_TO_DEVICE_WATCHER.
  agent_under_test_.AddToDeviceWatcher([](auto result) { EXPECT_TRUE(result.is_response()); });
  agent_under_test_.AddToDeviceWatcher([](auto result) {
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.err(),
              fuchsia::camera::test::virtualcamera::Error::ALREADY_ADDED_TO_DEVICE_WATCHER);
  });

  // Run loop until call to |InjectDevice| completes.
  RunLoopUntilIdle();
}

TEST_F(VirtualCameraAgentTest, TestConnectToHalController) {
  // Create a |StreamConfig| and inject the device.
  fuchsia::camera::test::virtualcamera::StreamConfig config;
  config.set_width(100);
  config.set_height(100);
  agent_under_test_.AddStreamConfig(0, fidl::Clone(config));
  agent_under_test_.AddToDeviceWatcher([](auto result) { EXPECT_TRUE(result.is_response()); });
  RunLoopUntilIdle();

  // Connect to the HAL controller. Verify it is bound.
  fidl::InterfacePtr<fuchsia::camera2::hal::Controller> hal_controller =
      device_watcher_tester_.GetHalController();
  RunLoopUntilIdle();
  EXPECT_TRUE(hal_controller);

  // Get the device info.
  hal_controller->GetDeviceInfo([](auto device_info) {
    EXPECT_EQ(device_info.vendor_name(), "virtual_camera_vendor");
    EXPECT_EQ(device_info.product_name(), "virtual_camera_product");
    EXPECT_EQ(device_info.vendor_id(), 1);
    EXPECT_EQ(device_info.product_id(), 2);
  });
  RunLoopUntilIdle();

  // Get the configs. There should only be one config so the second call should
  // return ZX_ERR_STOP
  hal_controller->GetNextConfig(
      [&config](std::unique_ptr<fuchsia::camera2::hal::Config> next_config, zx_status_t status) {
        EXPECT_EQ(status, ZX_OK);
        EXPECT_TRUE(next_config);

        camera::StreamConstraints constraints(fuchsia::camera2::CameraStreamType::MONITORING);
        constraints.AddImageFormat(config.width(), config.height(),
                                   fuchsia::sysmem::PixelFormatType::NV12);
        fuchsia::camera2::hal::Config expected_config;
        expected_config.stream_configs.push_back(constraints.ConvertToStreamConfig());
        EXPECT_TRUE(fidl::Equals(*next_config, expected_config));
      });
  RunLoopUntilIdle();

  hal_controller->GetNextConfig(
      [](std::unique_ptr<fuchsia::camera2::hal::Config> next_config, zx_status_t status) {
        EXPECT_EQ(status, ZX_ERR_STOP);
        EXPECT_FALSE(next_config);
      });
  RunLoopUntilIdle();
}

}  // namespace
