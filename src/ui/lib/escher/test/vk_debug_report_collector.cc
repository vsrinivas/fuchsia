// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/test/vk_debug_report_collector.h"

#include <gtest/gtest.h>

namespace escher::test {
namespace impl {

uint32_t VkDebugReportCollector::HandleDebugReport(VkFlags flags,
                                                   VkDebugReportObjectTypeEXT object_type,
                                                   uint64_t object, size_t location,
                                                   int32_t message_code, const char *layer_prefix,
                                                   const char *message, void *user_data) {
  vk::DebugReportFlagsEXT flags_ext(static_cast<vk::DebugReportFlagBitsEXT>(flags));
  vk::DebugReportObjectTypeEXT object_type_ext(
      static_cast<vk::DebugReportObjectTypeEXT>(object_type));

  auto &debug_reports = static_cast<VkDebugReportCollector *>(user_data)->debug_reports_;
  debug_reports.emplace_back(VkDebugReport{.flags = flags_ext,
                                           .object_type = object_type_ext,
                                           .object = object,
                                           .layer_prefix = layer_prefix,
                                           .message_code = message_code,
                                           .message = message});
  return false;
}

bool VkDebugReportCollector::PrintDebugReportsWithFlags(const vk::DebugReportFlagsEXT &flags,
                                                        const char *file, size_t line) {
  auto debug_reports_with_flags = DebugReportsWithFlag(flags);
  for (const auto &debug_report : debug_reports_with_flags) {
    GTEST_MESSAGE_AT_(file, line, debug_report.ErrorMessage().c_str(),
                      ::testing::TestPartResult::kNonFatalFailure);
  }
  return !debug_reports_with_flags.empty();
}

bool VkDebugReportCollector::PrintDebugReportsOnFalsePredicate(
    const vk::DebugReportFlagsEXT &flags, size_t num_threshold,
    const std::function<bool(size_t, size_t)> &pred, const char *file, size_t line) const {
  auto debug_reports_with_flags = DebugReportsWithFlag(flags);
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

std::vector<VkDebugReportCollector::VkDebugReport> VkDebugReportCollector::DebugReportsWithFlag(
    const vk::DebugReportFlagsEXT &flags) const {
  std::vector<VkDebugReport> result = {};
  std::copy_if(debug_reports_.begin(), debug_reports_.end(), std::back_inserter(result),
               [flags](const auto &report) { return report.flags & flags; });
  return result;
}

void VkDebugReportCollector::SuppressDebugReportsWithFlag(const vk::DebugReportFlagsEXT &flags) {
  auto end = std::remove_if(
      debug_reports_.begin(), debug_reports_.end(),
      [flags](const VkDebugReport &debug_report) { return flags & debug_report.flags; });
  debug_reports_.erase(end, debug_reports_.end());
}

}  // namespace impl
}  // namespace escher::test
