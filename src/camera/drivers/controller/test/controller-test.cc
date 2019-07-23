// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../controller.h"

#include <lib/fake_ddk/fake_ddk.h>

#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

namespace camera {

class ControllerTest : public Controller {
 public:
  ControllerTest()
      : Controller(fake_ddk::kFakeParent, fake_ddk::kFakeParent, fake_ddk::kFakeParent) {}
};

TEST(ControllerTest, DdkLifecycle) {
  ControllerTest test_controller;
  fake_ddk::Bind ddk;
  EXPECT_OK(test_controller.DdkAdd("test-camera-controller"));
  test_controller.DdkUnbind();
  EXPECT_TRUE(ddk.Ok());
}
}  // namespace camera
