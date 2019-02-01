// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/escher/test/gtest_escher.h"

#include "garnet/public/lib/escher/escher_process_init.h"

#include <vulkan/vulkan.hpp>

namespace escher {
namespace test {

static std::unique_ptr<Escher> g_escher;

Escher* GetEscher() {
  EXPECT_FALSE(VK_TESTS_SUPPRESSED());
  EXPECT_NE(g_escher.get(), nullptr);
  return g_escher.get();
}

void SetUpEscher() {
  if (!VK_TESTS_SUPPRESSED()) {
    ASSERT_EQ(g_escher.get(), nullptr);

    VulkanInstance::Params instance_params(
        {{"VK_LAYER_LUNARG_standard_validation"},
         {VK_EXT_DEBUG_REPORT_EXTENSION_NAME},
         false});

    VulkanDeviceQueues::Params device_params({{}, vk::SurfaceKHR()});

#ifdef OS_FUCHSIA
    device_params.extension_names.insert(
        VK_KHR_EXTERNAL_SEMAPHORE_FUCHSIA_EXTENSION_NAME);
#endif

    auto vulkan_instance = VulkanInstance::New(instance_params);

    auto vulkan_device =
        VulkanDeviceQueues::New(vulkan_instance, device_params);

    g_escher = std::make_unique<Escher>(vulkan_device);
  }

  escher::GlslangInitializeProcess();
}

void TearDownEscher() {
  escher::GlslangFinalizeProcess();

  if (!VK_TESTS_SUPPRESSED()) {
    g_escher = nullptr;
  }
}

}  // namespace test
}  // namespace escher
