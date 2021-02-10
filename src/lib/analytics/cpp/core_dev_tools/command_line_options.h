// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_COMMAND_LINE_OPTIONS_H_
#define SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_COMMAND_LINE_OPTIONS_H_

#include <lib/cmdline/args_parser.h>

#include <iostream>
#include <string>

#include "src/lib/analytics/cpp/core_dev_tools/analytics_status.h"

namespace analytics::core_dev_tools {

enum class AnalyticsOption { kEnable, kDisable, kSubLaunchFirst, kSubLaunchNormal, kUnspecified };

extern const char* const kAnalyticsHelp;
extern const char* const kAnalyticsShowHelp;

cmdline::Status ParseAnalyticsOption(const std::string& value, AnalyticsOption& analytics_option);

// Early processing of analytics options. Returns true if invoked with --analytics=enable|disable or
// --show-analytics, indicating that we are expected to exit after analytics related actions.
// T is the Analytics class for the tool.
template <class T>
bool EarlyProcessAnalyticsOptions(AnalyticsOption analytics_option, bool analytics_show) {
  bool should_exit_early = false;
  if (analytics_option == AnalyticsOption::kEnable) {
    T::PersistentEnable();
    should_exit_early = true;
  } else if (analytics_option == AnalyticsOption::kDisable) {
    T::PersistentDisable();
    should_exit_early = true;
  }

  if (analytics_show) {
    T::ShowAnalytics();
    should_exit_early = true;
  }

  return should_exit_early;
}

std::istream& operator>>(std::istream& is, AnalyticsOption& analytics_option);

}  // namespace analytics::core_dev_tools

#endif  // SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_COMMAND_LINE_OPTIONS_H_
