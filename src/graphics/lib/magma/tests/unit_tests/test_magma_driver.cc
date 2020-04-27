// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "helper/platform_device_helper.h"
#include "sys_driver/magma_driver.h"

class TestMagmaDriver {
 public:
  static void CreateAndDestroy() {
    auto driver = MagmaDriver::Create();
    EXPECT_NE(nullptr, driver);
  }
};

TEST(MagmaDriver, CreateAndDestroy) { TestMagmaDriver::CreateAndDestroy(); }
