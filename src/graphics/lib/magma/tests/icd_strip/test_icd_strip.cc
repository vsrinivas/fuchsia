// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>

#include <filesystem>

#include <gtest/gtest.h>

// Checks that the magma_vulkan_icd template generates shared libries
// that may contain the four necessary entry points.
TEST(IcdStrip, LoadEntryPoints) {
  // NOTE: dlopen() takes path relative to /lib.
  void* handle = dlopen("libicd_strip_test.so", RTLD_NOW);
  ASSERT_TRUE(handle) << " dlerror: " << dlerror();

  std::vector<const char*> entry_points = {
      "vk_icdGetInstanceProcAddr",
      "vk_icdGetPhysicalDeviceProcAddr",
      "vk_icdNegotiateLoaderICDInterfaceVersion",
      "vk_icdInitializeOpenInNamespaceCallback",
  };

  for (auto& entry_point : entry_points) {
    void* ptr = dlsym(handle, entry_point);
    EXPECT_TRUE(ptr) << " Couldn't find entry point: " << entry_point << " dlerror: " << dlerror();
  }

  dlclose(handle);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
