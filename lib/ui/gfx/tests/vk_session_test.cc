// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/vk_session_test.h"
#include "garnet/lib/ui/gfx/util/vulkan_utils.h"

namespace scenic {
namespace gfx {
namespace test {

std::unique_ptr<Engine> VkSessionTest::CreateEngine() {
  // Initialize Vulkan.
  escher::VulkanInstance::Params instance_params(
      {{}, {VK_EXT_DEBUG_REPORT_EXTENSION_NAME}, false});

  instance_params.layer_names.insert("VK_LAYER_LUNARG_standard_validation");
  auto vulkan_instance =
      escher::VulkanInstance::New(std::move(instance_params));
  auto vulkan_device = escher::VulkanDeviceQueues::New(
      vulkan_instance,
      {{VK_KHR_EXTERNAL_SEMAPHORE_FUCHSIA_EXTENSION_NAME}, vk::SurfaceKHR()});

  escher_ = std::make_unique<escher::Escher>(vulkan_device);

  return std::make_unique<EngineForTest>(&display_manager_, nullptr,
                                         escher_->GetWeakPtr());
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic
