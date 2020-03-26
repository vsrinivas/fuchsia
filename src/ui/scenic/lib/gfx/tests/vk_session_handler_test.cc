// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/tests/vk_session_handler_test.h"

#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/test/common/test_with_vk_validation_layer.h"

using namespace escher;

namespace scenic_impl {
namespace gfx {
namespace test {

VkSessionHandlerTest::VkSessionHandlerTest()
    : SessionHandlerTest(),
      vk_debug_report_callback_registry_(
          escher::test::EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanInstance(),
          std::make_optional<VulkanInstance::DebugReportCallback>(
              escher::test::impl::VkDebugReportCollector::HandleDebugReport,
              &vk_debug_report_collector_),
          {}),
      vk_debug_report_collector_() {}

void VkSessionHandlerTest::SetUp() {
  auto vulkan_device = CreateVulkanDeviceQueues(false);
  escher_ = std::make_unique<Escher>(vulkan_device);

  SessionHandlerTest::SetUp();
}

void VkSessionHandlerTest::TearDown() {
  SessionHandlerTest::TearDown();

  escher_->vk_device().waitIdle();
  EXPECT_TRUE(escher_->Cleanup());
  EXPECT_VULKAN_VALIDATION_OK();
}

escher::EscherWeakPtr VkSessionHandlerTest::GetEscherWeakPtr() { return escher_->GetWeakPtr(); }

VulkanDeviceQueuesPtr VkSessionHandlerTest::CreateVulkanDeviceQueues(bool use_protected_memory) {
  auto vulkan_instance =
      escher::test::EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanInstance();
  // This extension is necessary to support exporting Vulkan memory to a VMO.
  VulkanDeviceQueues::Params::Flags flags =
      use_protected_memory ? VulkanDeviceQueues::Params::kAllowProtectedMemory : 0;
  auto vulkan_queues = VulkanDeviceQueues::New(
      vulkan_instance,
      {{VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
        VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME},
       {},
       vk::SurfaceKHR(),
       flags});
  // Some devices might not be capable of using protected memory.
  if (use_protected_memory && !vulkan_queues->caps().allow_protected_memory) {
    return nullptr;
  }
  return vulkan_queues;
}
}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
