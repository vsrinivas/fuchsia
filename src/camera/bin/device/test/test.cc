// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include "src/camera/bin/device/device_impl.h"
#include "src/camera/bin/device/test/fake_controller.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

class DeviceTest : public gtest::TestLoopFixture {
 protected:
  DeviceTest() : context_(sys::ComponentContext::Create()) {}

  void SetUp() override {
    fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller;
    auto controller_result = FakeController::Create(controller.NewRequest());
    ASSERT_TRUE(controller_result.is_ok());
    controller_ = controller_result.take_value();

    auto device_result = DeviceImpl::Create(std::move(controller));
    ASSERT_TRUE(device_result.is_ok());
    device_ = device_result.take_value();
  }

  void TearDown() override {
    device_ = nullptr;
    controller_ = nullptr;
  }

  std::unique_ptr<sys::ComponentContext> context_;
  std::unique_ptr<DeviceImpl> device_;
  std::unique_ptr<FakeController> controller_;
};

// Placeholder for future tests. For now this just exercises SetUp and TearDown.
TEST_F(DeviceTest, Placeholder) {}
