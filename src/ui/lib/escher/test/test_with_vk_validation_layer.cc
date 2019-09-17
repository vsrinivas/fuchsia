// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/test/test_with_vk_validation_layer.h"

namespace escher::test {

VkBool32 TestWithVkValidationLayer::HandleDebugReport(
    VkDebugReportFlagsEXT flags_in, VkDebugReportObjectTypeEXT object_type_in, uint64_t object,
    size_t location, int32_t message_code, const char *pLayerPrefix, const char *pMessage,
    void *pUserData) {
  vk::DebugReportFlagsEXT flags(static_cast<vk::DebugReportFlagBitsEXT>(flags_in));
  vk::DebugReportObjectTypeEXT object_type(
      static_cast<vk::DebugReportObjectTypeEXT>(object_type_in));

  auto flags_str = vk::to_string(flags);
  auto &debug_reports = static_cast<TestWithVkValidationLayer *>(pUserData)->debug_reports_;
  debug_reports.emplace_back(VulkanDebugReport{.flags = flags,
                                               .object_type = object_type,
                                               .object = object,
                                               .layer_prefix = pLayerPrefix,
                                               .message_code = message_code,
                                               .message = pMessage});
  return false;
}

bool TestWithVkValidationLayer::ExpectDebugReportsPred_(
    const vk::DebugReportFlagsEXT &flags, size_t num_threshold,
    const std::function<bool(size_t, size_t)> &pred, const char *file, size_t line) const {
  auto debug_reports_with_flags = DebugReportsWithFlag_(flags);
  bool result = true;
  if (!pred(debug_reports_with_flags.size(), num_threshold)) {
    for (const auto &debug_report : debug_reports_with_flags) {
      GTEST_MESSAGE_AT_(file, line, debug_report.ErrorMessage().c_str(),
                        ::testing::TestPartResult::kNonFatalFailure);
    }
    result = false;
  }
  return result;
}

std::vector<TestWithVkValidationLayer::VulkanDebugReport>
TestWithVkValidationLayer::DebugReportsWithFlag_(
    const vk::DebugReportFlagsEXT &flags) const {
  std::vector<VulkanDebugReport> result = {};
  std::copy_if(debug_reports_.begin(), debug_reports_.end(), std::back_inserter(result),
               [flags](const auto &report) { return report.flags & flags; });
  return result;
}

void TestWithVkValidationLayer::SuppressDebugReportsWithFlag_(
    const vk::DebugReportFlagsEXT &flags) {
  auto end = std::remove_if(
      debug_reports_.begin(), debug_reports_.end(),
      [flags](const VulkanDebugReport &debug_report) { return flags & debug_report.flags; });
  debug_reports_.erase(end, debug_reports_.end());
}


void TestWithVkValidationLayer::TearDown() {
  EXPECT_VULKAN_VALIDATION_OK();
  TestWithVkValidationLayerBase::TearDown();
}

}  // namespace escher::test
