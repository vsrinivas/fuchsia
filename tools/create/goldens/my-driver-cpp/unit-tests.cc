// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake_ddk/fake_ddk.h>

#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

#include "tools/create/goldens/my-driver-cpp/my_driver_cpp.h"

namespace my_driver_cpp {

using inspect::InspectTestHelper;

class MyDriverCppTest : public InspectTestHelper, public zxtest::Test {
  void SetUp() override {}

  void TearDown() override {}

 protected:
  fake_ddk::Bind ddk_;
};

TEST_F(MyDriverCppTest, LifetimeTest) {
  auto device = new MyDriverCpp(fake_ddk::kFakeParent);
  ASSERT_OK(device->Bind());
  ASSERT_OK(ddk_.WaitUntilInitComplete());
  device->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());
  device->DdkRelease();
}

// TODO: `is_bound` is an example inspect property. Replace this test with inspect properties
// if any are added to the driver. Remove this test if no new inspect nodes/properties were added.
TEST_F(MyDriverCppTest, InspectTest) {
  auto device = new MyDriverCpp(fake_ddk::kFakeParent);
  // Verify is_bound = false.
  ASSERT_NO_FATAL_FAILURES(ReadInspect(device->inspect_vmo()));
  ASSERT_NO_FATAL_FAILURES(CheckProperty<inspect::BoolPropertyValue>(
      hierarchy().node(), "is_bound", inspect::BoolPropertyValue(false)));

  ASSERT_OK(device->Bind());

  // Verify is_bound = true.
  ASSERT_NO_FATAL_FAILURES(ReadInspect(device->inspect_vmo()));
  ASSERT_NO_FATAL_FAILURES(CheckProperty<inspect::BoolPropertyValue>(
      hierarchy().node(), "is_bound", inspect::BoolPropertyValue(true)));

  ASSERT_OK(ddk_.WaitUntilInitComplete());
  device->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());
  device->DdkRelease();
}

}  // namespace my_driver_cpp
