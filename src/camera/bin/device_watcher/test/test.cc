// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <fs/pseudo_dir.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

class DeviceWatcherTest : public gtest::TestLoopFixture {
 protected:
  DeviceWatcherTest() : context_(sys::ComponentContext::Create()) {}
  virtual void SetUp() override {
    ASSERT_EQ(context_->svc()->Connect(watcher_.NewRequest()), ZX_OK);
    watcher_.set_error_handler([](zx_status_t status) {
      ADD_FAILURE() << "DeviceWatcher server disconnected: " << status;
    });
    RunLoopUntilIdle();
  }

  virtual void TearDown() override {
    watcher_ = nullptr;
    RunLoopUntilIdle();
  }

  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::camera3::DeviceWatcherPtr watcher_;
};

TEST_F(DeviceWatcherTest, WatchDevicesFindsSherlockCamera) {
  bool watch_devices_returned = false;
  std::vector<fuchsia::camera3::WatchDevicesEvent> events_returned;
  watcher_->WatchDevices([&](std::vector<fuchsia::camera3::WatchDevicesEvent> events) {
    events_returned = std::move(events);
    watch_devices_returned = true;
  });
  while (!HasFailure() && !watch_devices_returned) {
    RunLoopUntilIdle();
  }
  if (events_returned.empty()) {
    // The initial check may return before the camera has been bound, so watch again if none were
    // found initially.
    watch_devices_returned = false;
    watcher_->WatchDevices([&](std::vector<fuchsia::camera3::WatchDevicesEvent> events) {
      events_returned = std::move(events);
      watch_devices_returned = true;
    });
    while (!HasFailure() && !watch_devices_returned) {
      RunLoopUntilIdle();
    }
  }
  ASSERT_EQ(events_returned.size(), 1u);
  ASSERT_TRUE(events_returned[0].is_added());
}
