// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_ANALYTICS_MESSAGES_H_
#define SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_ANALYTICS_MESSAGES_H_

#include <string>
#include <string_view>

#include "src/lib/analytics/cpp/core_dev_tools/analytics_status.h"

namespace analytics::core_dev_tools::internal {

struct ToolInfo {
  std::string tool_name;
  std::string enable_args;
  std::string disable_args;
  std::string status_args;
};

void ShowMessageFirstRunOfFirstTool(const ToolInfo& tool_info);
void ShowMessageFirstRunOfOtherTool(const ToolInfo& tool_info, AnalyticsStatus status);
void ShowAnalytics(const ToolInfo& tool_info, AnalyticsStatus status,
                   std::string_view analytics_list);

void ShowAlready(AnalyticsStatus status);
void ShowChangedTo(AnalyticsStatus status);

}  // namespace analytics::core_dev_tools::internal

#endif  // SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_ANALYTICS_MESSAGES_H_
