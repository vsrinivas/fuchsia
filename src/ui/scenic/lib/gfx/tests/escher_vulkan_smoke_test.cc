// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/test/common/gtest_vulkan.h"

VK_TEST(EscherVulkanSmokeTest, OnlyIfSupported) {
  // This test should not run if Vulkan tests are suppressed.
  EXPECT_FALSE(VK_TESTS_SUPPRESSED());
}
