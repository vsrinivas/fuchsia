// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

#include "tools/create/goldens/my-driver-cpp/my_driver_cpp.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace my_driver_cpp {

using inspect::InspectTestHelper;

class MyDriverCppTest : public InspectTestHelper, public zxtest::Test {
  void SetUp() override { fake_root_ = MockDevice::FakeRootParent(); }

  void TearDown() override {}

 protected:
  std::shared_ptr<zx_device> fake_root_;
};

TEST_F(MyDriverCppTest, LifetimeTest) {
  auto device = new MyDriverCpp(fake_root_.get());
  ASSERT_OK(device->Bind());
  device->zxdev()->InitOp();
  ASSERT_OK(device->zxdev()->WaitUntilInitReplyCalled(zx::time::infinite()));
  device->DdkAsyncRemove();
  ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(fake_root_.get()));
}

// TODO: `is_bound` is an example inspect property. Replace this test with inspect properties
// if any are added to the driver. Remove this test if no new inspect nodes/properties were added.
TEST_F(MyDriverCppTest, InspectTest) {
  auto device = new MyDriverCpp(fake_root_.get());
  // Verify is_bound = false.
  ASSERT_NO_FATAL_FAILURES(ReadInspect(device->inspect_vmo()));
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(hierarchy().node(), "is_bound", inspect::BoolPropertyValue(false)));

  ASSERT_OK(device->Bind());

  // Verify is_bound = true.
  ASSERT_NO_FATAL_FAILURES(ReadInspect(device->inspect_vmo()));
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(hierarchy().node(), "is_bound", inspect::BoolPropertyValue(true)));

  device->zxdev()->InitOp();
  ASSERT_OK(device->zxdev()->WaitUntilInitReplyCalled(zx::time::infinite()));
  device->DdkAsyncRemove();
  ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(fake_root_.get()));
}

}  // namespace my_driver_cpp
