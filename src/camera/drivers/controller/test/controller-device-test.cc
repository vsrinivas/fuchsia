// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../controller-device.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fit/function.h>

#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

#include "../controller-protocol.h"

namespace camera {

class ControllerDeviceTest : public ControllerDevice {
 public:
  ControllerDeviceTest()
      : ControllerDevice(fake_ddk::kFakeParent, fake_ddk::kFakeParent, fake_ddk::kFakeParent),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    ASSERT_OK(zx::channel::create(0u, &local_, &remote_));
    loop_.StartThread("camera-controller-loop", &loop_thread_);
  }

  ~ControllerDeviceTest() { loop_.Shutdown(); }

  zx::channel& local() { return local_; }
  zx::channel& remote() { return remote_; }
  fake_ddk::Bind& ddk() { return ddk_; }
  async::Loop& loop() { return loop_; }
  sync_completion_t& event() { return event_; };
  zx_handle_t DdkFidlChannel() { return ddk_.FidlClient().get(); }
  fuchsia::camera2::hal::ControllerPtr& controller_protocol() { return controller_protocol_; }

  void GetConfigCallback(::fidl::VectorPtr<fuchsia::camera2::hal::Config> configs,
                         zx_status_t status) {
    ASSERT_OK(status);
    EXPECT_TRUE(configs.has_value());
    EXPECT_GT(configs->size(), 0u);
    sync_completion_signal(&event_);
  }

  void GetDeviceInfoCallback(::fuchsia::camera2::DeviceInfo device_info) {
    EXPECT_EQ(kCameraVendorName, device_info.vendor_name());
    EXPECT_EQ(kCameraProductName, device_info.product_name());
    EXPECT_EQ(fuchsia::camera2::DeviceType::BUILTIN, device_info.type());
    sync_completion_signal(&event_);
  }

  void SetupForControllerProtocol() {
    StartThread();
    ASSERT_OK(DdkAdd("test-camera-controller"));
    ASSERT_OK(fuchsia_hardware_camera_DeviceGetChannel2(DdkFidlChannel(), remote().release()));
    controller_protocol().Bind(std::move(local()), loop().dispatcher());
  }

 private:
  fake_ddk::Bind ddk_;
  zx::channel local_, remote_;
  fbl::unique_ptr<ControllerDevice> controller_;
  thrd_t loop_thread_;
  sync_completion_t event_;
  fuchsia::camera2::hal::ControllerPtr controller_protocol_;
  async::Loop loop_;
};

TEST(ControllerDeviceTest, DdkLifecycle) {
  ControllerDeviceTest test_controller;
  EXPECT_OK(test_controller.DdkAdd("test-camera-controller"));
  test_controller.DdkUnbindDeprecated();
  EXPECT_TRUE(test_controller.ddk().Ok());
}

// Test to ensure that we do not support
// GetChannel API
TEST(ControllerDeviceTest, GetChannel) {
  ControllerDeviceTest test_controller;
  ASSERT_OK(test_controller.DdkAdd("test-camera-controller"));
  ASSERT_OK(fuchsia_hardware_camera_DeviceGetChannel(test_controller.DdkFidlChannel(),
                                                     test_controller.remote().release()));
  zx_signals_t observed;
  EXPECT_EQ(ZX_OK, test_controller.local().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(),
                                                    &observed));
}

TEST(ControllerDeviceTest, GetChannel2) {
  ControllerDeviceTest test_controller;
  ASSERT_OK(test_controller.DdkAdd("test-camera-controller"));
  EXPECT_OK(fuchsia_hardware_camera_DeviceGetChannel2(test_controller.DdkFidlChannel(),
                                                      test_controller.remote().release()));
}

TEST(ControllerDeviceTest, GetChannel2InvalidHandle) {
  ControllerDeviceTest test_controller;
  ASSERT_OK(test_controller.DdkAdd("test-camera-controller"));
  EXPECT_OK(fuchsia_hardware_camera_DeviceGetChannel2(test_controller.DdkFidlChannel(),
                                                      test_controller.remote().release()));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            fuchsia_hardware_camera_DeviceGetChannel2(test_controller.DdkFidlChannel(),
                                                      test_controller.remote().release()));
}

TEST(ControllerDeviceTest, GetChannel2InvokeTwice) {
  ControllerDeviceTest test_controller;
  ASSERT_OK(test_controller.DdkAdd("test-camera-controller"));
  EXPECT_OK(fuchsia_hardware_camera_DeviceGetChannel2(test_controller.DdkFidlChannel(),
                                                      test_controller.remote().release()));

  zx::channel new_local, new_remote;
  ASSERT_OK(zx::channel::create(0u, &new_local, &new_remote));
  EXPECT_OK(fuchsia_hardware_camera_DeviceGetChannel2(test_controller.DdkFidlChannel(),
                                                      new_remote.release()));
  zx_signals_t observed;
  EXPECT_EQ(ZX_OK, new_local.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &observed));
}

TEST(ControllerDeviceTest, GetDeviceInfo) {
  ControllerDeviceTest test_controller;
  test_controller.SetupForControllerProtocol();
  test_controller.controller_protocol()->GetDeviceInfo(
      fit::bind_member(&test_controller, &ControllerDeviceTest::GetDeviceInfoCallback));
  sync_completion_wait(&test_controller.event(), ZX_TIME_INFINITE);
}

TEST(ControllerDeviceTest, GetConfigs) {
  ControllerDeviceTest test_controller;
  test_controller.SetupForControllerProtocol();
  test_controller.controller_protocol()->GetConfigs(
      fit::bind_member(&test_controller, &ControllerDeviceTest::GetConfigCallback));
  sync_completion_wait(&test_controller.event(), ZX_TIME_INFINITE);
}
}  // namespace camera
