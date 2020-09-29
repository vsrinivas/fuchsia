// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>

#include <filesystem>

#include <gtest/gtest.h>

// Checks that the magma_vulkan_icd template generates shared libries
// that may contain the four necessary entry points.
TEST(IcdStrip, LoadEntryPoints) {
  void* handle = nullptr;

  // The ICD is packaged under /pkg/lib/x64-shared or /pkg/lib/arm64-shared
  // Beware of other subdirectories (such as asan).
  for (auto& dir : std::filesystem::directory_iterator("/pkg/lib")) {
    if (!dir.is_directory())
      continue;
    for (auto& file : std::filesystem::directory_iterator(dir.path())) {
      if (file.path().filename() != "libicd_strip_test.so") {
        continue;
      }
      // dlopen takes path relative to /lib
      auto lib = dir.path().stem() / file.path().filename();
      handle = dlopen(lib.c_str(), RTLD_NOW);
      ASSERT_TRUE(handle) << " dlerror: " << dlerror();
      break;
    }
  }

  ASSERT_TRUE(handle);

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
