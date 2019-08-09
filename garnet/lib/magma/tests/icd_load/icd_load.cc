// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "icd_load.h"

#include <cstdint>
#include <cstdio>
#include <vector>

#include <vulkan/vulkan.h>

#include "gtest/gtest.h"
#include "magma_util/dlog.h"
#include "src/lib/fxl/test/test_settings.h"

void IcdLoadTest::LoadIcd() {
  // vkEnumerateInstanceExtensionProperties is the chosen entrypoint because
  // it doesn't require an instance parameter.
  uint32_t num_extensions = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &num_extensions, nullptr);

  std::vector<VkExtensionProperties> extensions(num_extensions);
  vkEnumerateInstanceExtensionProperties(nullptr, &num_extensions, extensions.data());

  // The VCD must be loaded for VK_KHR_get_physical_device_properties2
  // to be available.
  const char* kRequiredExtension = "VK_KHR_get_physical_device_properties2";
  bool found_extension = false;
  for (const auto& extension : extensions) {
    DLOG("%s\n", extension.extensionName);
    if (!strcmp(extension.extensionName, kRequiredExtension)) {
      found_extension = true;
      break;
    }
  }
  EXPECT_TRUE(found_extension);
}

TEST(Vulkan, IcdLoad) {
  IcdLoadTest test;
  test.LoadIcd();
}

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
