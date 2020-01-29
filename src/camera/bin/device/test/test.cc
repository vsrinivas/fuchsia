// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include "src/camera/bin/device/device_impl.h"
#include "src/camera/bin/device/stream_impl.h"
#include "src/camera/bin/device/test/fake_controller.h"
#include "src/camera/bin/device/util.h"
#include "src/camera/lib/fake_stream/fake_stream.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace camera {

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

TEST_F(DeviceTest, CreateStreamNullConnection) { StreamImpl stream(nullptr); }

TEST_F(DeviceTest, CreateStreamFakeConnection) {
  fidl::InterfaceHandle<fuchsia::camera2::Stream> handle;
  auto result = FakeStream::Create(handle.NewRequest());
  ASSERT_TRUE(result.is_ok());
  { StreamImpl stream(std::move(handle)); }
}

TEST_F(DeviceTest, ConvertConfig) {
  auto configs = FakeController::GetDefaultConfigs();
  ASSERT_FALSE(configs.empty());
  auto& a = configs[0];
  ASSERT_FALSE(a.stream_configs.empty());
  ASSERT_FALSE(a.stream_configs[0].image_formats.empty());
  auto result = Convert(a);
  ASSERT_TRUE(result.is_ok());
  auto b = result.take_value();
  EXPECT_EQ(a.stream_configs.size(), b.streams.size());
  ASSERT_FALSE(b.streams[0].supported_resolutions.empty());
  EXPECT_EQ(a.stream_configs[0].image_formats[0].bytes_per_row,
            b.streams[0].supported_resolutions[0].bytes_per_row);
}

}  // namespace camera
