// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera/test/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/fake_launcher.h>

#include "src/camera/bin/device_watcher/device_instance.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

class DeviceWatcherTest : public gtest::TestLoopFixture {
 protected:
  DeviceWatcherTest() : context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {}
  void SetUp() override {
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

  void TearDown() override {
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

class FakeCamera : public fuchsia::hardware::camera::Device,
                   public fuchsia::camera2::hal::Controller {
 public:
  explicit FakeCamera(fidl::InterfaceRequest<fuchsia::hardware::camera::Device> request)
      : camera_binding_(this, std::move(request)), controller_binding_(this) {}
  void GetChannel(zx::channel channel) override {}
  void GetChannel2(zx::channel channel) override {
    ZX_ASSERT(controller_binding_.Bind(std::move(channel)) == ZX_OK);
  }
  void GetNextConfig(fuchsia::camera2::hal::Controller::GetNextConfigCallback callback) override {}
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
  fidl::Binding<fuchsia::hardware::camera::Device> camera_binding_;
  fidl::Binding<fuchsia::camera2::hal::Controller> controller_binding_;
};

// TODO(fxbug.dev/53130): fix device_watcher_test flake
TEST_F(DeviceWatcherTest, DISABLED_WatchDevicesFindsCameras) {
  fidl::InterfaceHandle<fuchsia::hardware::camera::Device> camera;
  FakeCamera fake(camera.NewRequest());
  tester_->InjectDevice(std::move(camera));
  std::set<uint64_t> cameras;

  // Wait until the watcher has discovered the real camera and the injected fake camera.
  constexpr uint32_t kExpectedCameras = 2;
  while (!HasFailure() && cameras.size() < kExpectedCameras) {
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

  // Ensure that a second watcher client is given the same cameras.
  fuchsia::camera3::DeviceWatcherPtr watcher2;
  ASSERT_EQ(context_->svc()->Connect(watcher2.NewRequest()), ZX_OK);
  watcher2.set_error_handler(
      [](zx_status_t status) { ADD_FAILURE() << "DeviceWatcher server disconnected: " << status; });
  while (!HasFailure() && !cameras.empty()) {
    bool watch_devices_returned = false;
    watcher2->WatchDevices([&](std::vector<fuchsia::camera3::WatchDevicesEvent> events) {
      for (auto& event : events) {
        ASSERT_TRUE(event.is_added());
        auto it = cameras.find(event.added());
        ASSERT_NE(it, cameras.end());
        cameras.erase(it);
      }
      watch_devices_returned = true;
    });
    while (!HasFailure() && !watch_devices_returned) {
      RunLoopUntilIdle();
    }
  }
}

TEST_F(DeviceWatcherTest, InstanceLaunches) {
  sys::testing::FakeLauncher fake_launcher;
  constexpr auto kCameraDeviceUrl =
      "fuchsia-pkg://fuchsia.com/camera_device#meta/camera_device.cmx";
  bool camera_launched = false;
  fake_launcher.RegisterComponent(
      kCameraDeviceUrl, [&](fuchsia::sys::LaunchInfo launch_info,
                            fidl::InterfaceRequest<fuchsia::sys::ComponentController> request) {
        EXPECT_EQ(launch_info.url, kCameraDeviceUrl);
        ASSERT_TRUE(launch_info.arguments.has_value());
        EXPECT_EQ(launch_info.arguments.value().size(), 1u);
        request.Close(ZX_ERR_PEER_CLOSED);
        camera_launched = true;
      });
  fuchsia::sys::LauncherPtr launcher;
  fake_launcher.GetHandler()(launcher.NewRequest());
  bool component_unavailable_received = false;
  auto result =
      DeviceInstance::Create(launcher, nullptr, [&]() { component_unavailable_received = true; });
  ASSERT_TRUE(result.is_ok());
  auto instance = result.take_value();
  // The instance should attempt to launch the component. Then, upon seeing the controller request
  // close, it should call the component_unavailable callback.
  while (!HasFailure() && (!camera_launched || !component_unavailable_received)) {
    RunLoopUntilIdle();
  }
}
