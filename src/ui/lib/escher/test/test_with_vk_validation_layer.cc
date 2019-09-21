// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/test/test_with_vk_validation_layer.h"

#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/test/gtest_vulkan.h"

namespace escher::test {

TestWithVkValidationLayer::TestWithVkValidationLayer(
    std::vector<VulkanInstance::DebugReportCallback> optional_callbacks)
    : vk_debug_report_callback_registry_(
          VK_TESTS_SUPPRESSED()
              ? nullptr
              : EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanInstance(),
          std::make_optional<VulkanInstance::DebugReportCallback>(
              impl::VkDebugReportCollector::HandleDebugReport, &vk_debug_report_collector_),
          std::move(optional_callbacks)),
      vk_debug_report_collector_() {}

void TestWithVkValidationLayer::SetUp() {
  vk_debug_report_callback_registry().RegisterDebugReportCallbacks();
}

void TestWithVkValidationLayer::TearDown() {
  EXPECT_VULKAN_VALIDATION_OK();
  vk_debug_report_callback_registry().DeregisterDebugReportCallbacks();
}

}  // namespace escher::test
