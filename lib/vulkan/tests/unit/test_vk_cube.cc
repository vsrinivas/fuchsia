// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_vk_cube.h"
#include "gtest/gtest.h"

TEST(MagmaIntegration, VkCube) {
  constexpr int argc = 4;
  char argv0[] = "vkcube_integration_test";
  char argv1[] = "--c";
  char argv2[] = "1";
  char argv3[] = "--validate";
  char* argv[argc] = {argv0, argv1, argv2, argv3};
  int ret = test_vk_cube(argc, argv);
  EXPECT_EQ(0, ret);
}
