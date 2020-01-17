// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
    RunLoopUntilIdle();
  }

  virtual void TearDown() override {
    watcher_ = nullptr;
    RunLoopUntilIdle();
  }

  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::camera3::DeviceWatcherPtr watcher_;
};

TEST_F(DeviceWatcherTest, Placeholder) {
  bool watch_devices_returned = false;
  watcher_->WatchDevices([&](std::vector<fuchsia::camera3::WatchDevicesEvent> events) {
    EXPECT_TRUE(events.empty());
    watch_devices_returned = true;
  });
  while (!HasFailure() && !watch_devices_returned) {
    RunLoopUntilIdle();
  }
}
