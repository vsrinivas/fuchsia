// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/camera/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/real_loop_fixture.h>

#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>
#include <src/camera/camera_manager2/camera_plug_detector.h>
#include <src/lib/fsl/io/device_watcher.h>

#include "device_creation_fixture.h"
#include "src/lib/syslog/cpp/logger.h"

namespace camera {
namespace {

// A minimal |fuchsia::hardware::camera::Device| that we can use to emulate a fake devfs directory
// for testing.
class FakeCameraDevice : public fuchsia::hardware::camera::Device {
 public:
  // Checks that the |client_side| channel is the other end of our server_side_
  // channel.
  bool CheckMatchingChannel(const zx::channel& client_side) const {
    zx_info_handle_basic_t info[2];
    EXPECT_EQ(ZX_OK, client_side.get_info(ZX_INFO_HANDLE_BASIC, &info[0],
                                          sizeof(zx_info_handle_basic_t), nullptr, nullptr));
    EXPECT_EQ(ZX_OK, server_side_.get_info(ZX_INFO_HANDLE_BASIC, &info[1],
                                           sizeof(zx_info_handle_basic_t), nullptr, nullptr));
    EXPECT_NE(info[0].koid, 0u);
    EXPECT_NE(info[1].koid, 0u);

    return (info[0].related_koid == info[1].koid) && (info[1].related_koid == info[0].koid);
  }

  bool HasConnection() { return server_side_.is_valid(); }

 private:
  void GetChannel(zx::channel c) override {}
  void GetChannel2(zx::channel c) override { server_side_ = std::move(c); }

  zx::channel server_side_;
};

// DeviceTracker acts like the CameraManagerApp, keeping track of device connections.
// This allows us to gather all the plug events and make sure they were handled correctly.
class DeviceTracker {
 public:
  fit::function<void(fidl::InterfaceHandle<fuchsia::camera2::hal::Controller>)> GetHandler() {
    return [this](auto handler) {
      FX_LOGS(INFO) << "Handler called";
      controller_handles_.push_back(std::move(handler));
    };
  }

  size_t size() const { return controller_handles_.size(); }

  bool HasMatchingChannel(const FakeCameraDevice& device) {
    for (auto& handle : controller_handles_) {
      if (device.CheckMatchingChannel(handle.channel())) {
        return true;
      }
    }
    return false;
  }

 private:
  std::vector<fidl::InterfaceHandle<fuchsia::camera2::hal::Controller>> controller_handles_;
};

char path[] = "/dev/class/camera";
using CameraDeviceCreationTest = DeviceCreationFixture<path, fuchsia::hardware::camera::Device>;

TEST_F(CameraDeviceCreationTest, DetectExistingDevices) {
  // Add some devices that will exist before the plug detector starts.
  FakeCameraDevice camera0, camera1;
  auto d1 = AddDevice(&camera0);
  auto d2 = AddDevice(&camera1);

  // Create the plug detector; no events should be sent until |Start|.
  DeviceTracker tracker;
  CameraPlugDetector plug_detector;
  RunLoopUntilIdle();
  EXPECT_EQ(0u, tracker.size());

  // Start the detector; expect 2 events (1 for each device above);
  ASSERT_EQ(ZX_OK, plug_detector.Start(tracker.GetHandler()));
  RunLoopUntil([&tracker] { return tracker.size() >= 2; });
  EXPECT_EQ(2u, tracker.size());
  RunLoopUntil([&camera0] { return camera0.HasConnection(); });
  RunLoopUntil([&camera1] { return camera1.HasConnection(); });
  EXPECT_TRUE(tracker.HasMatchingChannel(camera0));
  EXPECT_TRUE(tracker.HasMatchingChannel(camera1));

  plug_detector.Stop();
}

TEST_F(CameraDeviceCreationTest, DetectHotplugDevices) {
  DeviceTracker tracker;
  CameraPlugDetector plug_detector;
  ASSERT_EQ(ZX_OK, plug_detector.Start(tracker.GetHandler()));
  RunLoopUntilIdle();
  EXPECT_EQ(0u, tracker.size());

  // Hotplug a device.
  FakeCameraDevice camera0;
  auto d1 = AddDevice(&camera0);
  RunLoopUntil([&tracker] { return tracker.size() >= 1u; });
  ASSERT_EQ(1u, tracker.size());
  RunLoopUntil([&camera0] { return camera0.HasConnection(); });
  EXPECT_TRUE(tracker.HasMatchingChannel(camera0));

  plug_detector.Stop();
}

}  // namespace
}  // namespace camera
