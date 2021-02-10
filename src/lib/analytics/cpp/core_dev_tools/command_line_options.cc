// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/core_dev_tools/command_line_options.h"

namespace analytics::core_dev_tools {

const char* const kAnalyticsHelp = R"(  --analytics=enable|disable
      Enable or disable collection of analytics:
      --analytics=enable           Enable collection of analytics and save the
                                   status in a configuration file.
      --analytics=disable          Disable collection of analytics and save the
                                   status in a configuration file.)";

const char* const kAnalyticsShowHelp = R"(  --analytics-show
      Show the opt-in/out status for collection of analytics and what we collect when opt-in.)";

// The analytics option can take two additional internal options, which can only be used when
// another core developer tool (such as ffx) is sub-launching the current tool. The two internal
// options are
//      --analytics=sublaunch-first  Indicate that the current tool is sub-launched by the
//                                   first run of the first tool. Collection of
//                                   analytics will be disabled in this run.
//      --analytics=sublaunch-normal Indicate that the current tool is sub-launched by another
//                                   tool, but not by the first run of the first
//                                   tool. Collection of analytics will be enabled
//                                   or disabled according to the saved status.
std::istream& operator>>(std::istream& is, AnalyticsOption& analytics_option) {
  std::string analytics_string;
  if (!(is >> analytics_string)) {
    return is;
  }
  if (analytics_string == "enable") {
    analytics_option = AnalyticsOption::kEnable;
  } else if (analytics_string == "disable") {
    analytics_option = AnalyticsOption::kDisable;
  } else if (analytics_string == "sublaunch-first") {
    analytics_option = AnalyticsOption::kSubLaunchFirst;
  } else if (analytics_string == "sublaunch-normal") {
    analytics_option = AnalyticsOption::kSubLaunchNormal;
  } else {
    is.setstate(std::ios::failbit);
  }
  return is;
}

}  // namespace analytics::core_dev_tools
