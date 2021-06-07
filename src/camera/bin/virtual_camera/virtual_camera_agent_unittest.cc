// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/virtual_camera/virtual_camera_agent.h"

#include <fuchsia/camera/test/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace {

class FakeDeviceWatcherTester : public fuchsia::camera::test::DeviceWatcherTester {
 public:
  MOCK_METHOD(void, InjectDevice, (fidl::InterfaceHandle<fuchsia::hardware::camera::Device>),
              (override));

  fidl::InterfaceRequestHandler<fuchsia::camera::test::DeviceWatcherTester> GetInterfaceHandler() {
    return bindings_.GetHandler(this);
  }

 private:
  fidl::BindingSet<fuchsia::camera::test::DeviceWatcherTester> bindings_;
};

class VirtualCameraAgentTest : public gtest::TestLoopFixture {
 protected:
  VirtualCameraAgentTest() : provider_(), agent_under_test_(provider_.context()) {}

  void SetUp() override {
    gtest::TestLoopFixture::SetUp();

    // Create a |DeviceWatchterTester| and publish it.
    provider_.service_directory_provider()->AddService<fuchsia::camera::test::DeviceWatcherTester>(
        device_watcher_tester_.GetInterfaceHandler());
  }

  sys::testing::ComponentContextProvider provider_;
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

  // Call AddToDeviceWatcher twice. The first call should succeed. The second
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

}  // namespace
