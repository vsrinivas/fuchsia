// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera/test/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

class DeviceWatcherTest : public gtest::TestLoopFixture {
 protected:
  DeviceWatcherTest() : context_(sys::ComponentContext::Create()) {}
  virtual void SetUp() override {
    ASSERT_EQ(context_->svc()->Connect(watcher_.NewRequest()), ZX_OK);
    watcher_.set_error_handler([](zx_status_t status) {
      ADD_FAILURE() << "DeviceWatcher server disconnected: " << status;
    });
    ASSERT_EQ(context_->svc()->Connect(tester_.NewRequest()), ZX_OK);
    tester_.set_error_handler([](zx_status_t status) {
      ADD_FAILURE() << "DeviceWatcherTester server disconnected: " << status;
    });
    RunLoopUntilIdle();
  }

  virtual void TearDown() override {
    tester_ = nullptr;
    watcher_ = nullptr;
    RunLoopUntilIdle();
  }

  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::camera3::DeviceWatcherPtr watcher_;
  fuchsia::camera::test::DeviceWatcherTesterPtr tester_;
};

constexpr uint16_t kFakeVendorId = 0xFFFF;
constexpr uint16_t kFakeProductId = 0xABCD;

class FakeCamera : public fuchsia::camera2::hal::Controller {
 public:
  FakeCamera(fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> request)
      : binding_(this, std::move(request)) {}
  void GetConfigs(fuchsia::camera2::hal::Controller::GetConfigsCallback callback) override {}
  void CreateStream(uint32_t config_index, uint32_t stream_index, uint32_t image_format_index,
                    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection,
                    fidl::InterfaceRequest<fuchsia::camera2::Stream> stream) override {}
  void EnableStreaming() override {}
  void DisableStreaming() override {}
  void GetDeviceInfo(fuchsia::camera2::hal::Controller::GetDeviceInfoCallback callback) override {
    fuchsia::camera2::DeviceInfo info{};
    info.set_vendor_id(kFakeVendorId);
    info.set_product_id(kFakeProductId);
    callback(std::move(info));
  }

 private:
  fidl::Binding<fuchsia::camera2::hal::Controller> binding_;
};

TEST_F(DeviceWatcherTest, WatchDevicesFindsCameras) {
  fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller;
  FakeCamera fake(controller.NewRequest());
  tester_->InjectDevice(std::move(controller));
  std::set<uint64_t> cameras;

  // Wait until the watcher has discovered the real camera and the injected fake camera.
  constexpr uint32_t kExpectedCameras = 2;
  while (cameras.size() < kExpectedCameras) {
    bool watch_devices_returned = false;
    watcher_->WatchDevices([&](std::vector<fuchsia::camera3::WatchDevicesEvent> events) {
      for (auto& event : events) {
        if (event.is_added()) {
          EXPECT_EQ(cameras.find(event.added()), cameras.end());
          cameras.insert(event.added());
        }
        EXPECT_FALSE(event.is_removed());
      }
      watch_devices_returned = true;
    });
    while (!HasFailure() && !watch_devices_returned) {
      RunLoopUntilIdle();
    }
  }
  ASSERT_EQ(cameras.size(), kExpectedCameras);
}
