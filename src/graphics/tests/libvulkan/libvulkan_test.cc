// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

#include "src/lib/fxl/test/test_settings.h"

TEST(Libvulkan, LoadIcd) {
  VkInstanceCreateInfo create_info{};
  VkInstance instance;
  // libvulkan_fake will be opened and should validate that it was called correctly.
  ASSERT_EQ(VK_SUCCESS, vkCreateInstance(&create_info, nullptr, &instance));

  vkDestroyInstance(instance, nullptr);
}

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
