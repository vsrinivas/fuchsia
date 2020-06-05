// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake_ddk/fake_ddk.h>

#include <zxtest/zxtest.h>

#include "tools/create/goldens/my-driver-cpp/my_driver_cpp.h"

namespace my_driver_cpp {

class MyDriverCppTest : public zxtest::Test {
  void SetUp() override {}

  void TearDown() override {}

 protected:
  fake_ddk::Bind ddk_;
};

TEST_F(MyDriverCppTest, LifetimeTest) {
  auto device = new MyDriverCpp(fake_ddk::kFakeParent);
  ASSERT_OK(device->Bind());
  device->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());
  device->DdkRelease();
}

}  // namespace my_driver_cpp
