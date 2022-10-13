// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

#include "src/connectivity/ethernet/drivers/gvnic/gvnic.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace gvnic {

using inspect::InspectTestHelper;

class GvnicTest : public InspectTestHelper, public zxtest::Test {
  void SetUp() override { fake_root_ = MockDevice::FakeRootParent(); }

  void TearDown() override {}

 protected:
  std::shared_ptr<zx_device> fake_root_;
};

TEST_F(GvnicTest, LifetimeTest) {
  // auto device = new Gvnic(fake_root_.get());
  // ASSERT_OK(device->Bind());
  // device->zxdev()->InitOp();
  // ASSERT_OK(device->zxdev()->WaitUntilInitReplyCalled(zx::time::infinite()));
  // device->DdkAsyncRemove();
  // ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(fake_root_.get()));
}

TEST_F(GvnicTest, InspectTest) {
  // auto device = new Gvnic(fake_root_.get());
  // // Verify is_bound = false.
  // ASSERT_NO_FATAL_FAILURE(ReadInspect(device->inspect_vmo()));
  // ASSERT_NO_FATAL_FAILURE(
  //     CheckProperty(hierarchy().node(), "is_bound", inspect::BoolPropertyValue(false)));

  // ASSERT_OK(device->Bind());

  // // Verify is_bound = true.
  // ASSERT_NO_FATAL_FAILURE(ReadInspect(device->inspect_vmo()));
  // ASSERT_NO_FATAL_FAILURE(
  //     CheckProperty(hierarchy().node(), "is_bound", inspect::BoolPropertyValue(true)));

  // device->zxdev()->InitOp();
  // ASSERT_OK(device->zxdev()->WaitUntilInitReplyCalled(zx::time::infinite()));
  // device->DdkAsyncRemove();
  // ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(fake_root_.get()));
}

}  // namespace gvnic
