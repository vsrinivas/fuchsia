// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/core_dev_tools/analytics_messages.h"

#include <iostream>

#include "src/lib/fxl/strings/substitute.h"

namespace analytics::core_dev_tools::internal {

namespace {

constexpr char kParticipatingTools[] = R"(  • device_launcher
  • ffx
  • fidlcat
  • Fuchsia extension for VS Code
  • Fuchsia Snapshot Viewer
  • scrutiny verify routes
  • symbolizer
  • zxdb)";

// In the following message:
// $0: list of participating tools
// $1: tool name
// $2: disable args
// $3: status args
constexpr char kMessageFirstRunOfFirstTool[] = R"(Welcome to Fuchsia! - https://fuchsia.dev

Fuchsia developer tools, including
$0
use Google Analytics to report feature usage statistics and basic crash reports.
Google may examine the collected data in aggregate to help improve these tools,
other Fuchsia tools, and the Fuchsia SDK.

Analytics are not sent on this very first run. To disable reporting, type
    $1 $2
To display the current setting and what is collected, type
    $1 $3
If you opt out of analytics, an opt-out event will be sent, and then no further
information will be sent by the Fuchsia developer tools to Google.

By using Fuchsia developer tools, you agree to the Google Terms of Service.
Note: The Google Privacy Policy describes how data is handled in your use of
this service.

See Google's privacy policy:
https://policies.google.com/privacy
)";

// In the following message:
// $0: tool name
// $1: disable args
// $2: status args
constexpr char kMessageFirstRunOfOtherToolEnabled[] = R"(Welcome to $0!

As part of the Fuchsia developer tools, this tool uses Google Analytics to
report feature usage statistics and basic crash reports. Google may examine the
collected data in aggregate to help improve the developer tools, other
Fuchsia tools, and the Fuchsia SDK.

To disable reporting, type
    $0 $1
To display the current setting, a full list of tools sharing this setting, and
what is collected, type
    $0 $2
If you opt out of analytics, an opt-out event will be sent, and then no further
information will be sent by the Fuchsia developer tools to Google.

See Google's privacy policy:
https://policies.google.com/privacy
)";

// In the following message:
// $0: tool name
// $1: enable args
// $2: status args
constexpr char kMessageFirstRunOfOtherToolDisabled[] = R"(Welcome to $0!

Analytics is currently disabled for Fuchsia developer tools, so no
information will be sent to Google from these tools.

If you would like to help improve the Fuchsia developer tools, other Fuchsia tools,
and the Fuchsia SDK via allowing us to report feature usage statistics and basic
crash reports to Google, you can type
    $0 $1
To display the current setting, a full list of tools sharing this setting, and
what is collected, type
    $0 $2
Thank you!

See Google's privacy policy:
https://policies.google.com/privacy
)";

// In the following message:
// $0: disabled/enabled
// $1: enable/disable
// $2: list of participating tools
// $3: tool name
// $4: enable args / disable args
// $5: list of tool-specific analytics
constexpr char kMessageShowAnalytics[] =
    R"(The collection of analytics is currently $0 for Fuchsia developer
tools, including
$2

To $1 analytics for all these tools, type
    $3 $4

When enabled, a random unique user ID (UUID) will be created for the current
user and it is used to collect some anonymized analytics of the session and user
workflow in order to improve the user experience. The analytics collected by
$3 are:

$5

When analytics is disabled, any existing UUID is deleted, and a new
UUID will be created if analytics is later re-enabled.

When enabled, the UUID and the status are stored in $HOME/.fuchsia
)";

// In the following message
// $0: enabled/disabled
constexpr char kMessageShowAlready[] =
    R"(Collection of analytics for Fuchsia developer tools is already $0.)";

// In the following message
// $0: enabled/disabled
constexpr char kMessageShowChangedTo[] =
    R"(Collection of analytics for Fuchsia developer tools is $0 at user's
request.)";

}  // namespace

void ShowMessageFirstRunOfFirstTool(const ToolInfo& tool_info) {
  std::cerr << fxl::Substitute(kMessageFirstRunOfFirstTool, kParticipatingTools,
                               tool_info.tool_name, tool_info.disable_args, tool_info.status_args)
            << std::endl;
}

void ShowMessageFirstRunOfOtherTool(const ToolInfo& tool_info, AnalyticsStatus status) {
  if (status == AnalyticsStatus::kEnabled) {
    std::cerr << fxl::Substitute(kMessageFirstRunOfOtherToolEnabled, tool_info.tool_name,
                                 tool_info.disable_args, tool_info.status_args)
              << std::endl;
  } else {
    std::cerr << fxl::Substitute(kMessageFirstRunOfOtherToolDisabled, tool_info.tool_name,
                                 tool_info.enable_args, tool_info.status_args)
              << std::endl;
  }
}

void ShowAnalytics(const ToolInfo& tool_info, AnalyticsStatus status,
                   std::string_view analytics_list) {
  bool is_enabled = (status == AnalyticsStatus::kEnabled);
  std::cout << fxl::Substitute(
                   kMessageShowAnalytics, is_enabled ? "enabled" : "disabled",
                   is_enabled ? "disable" : "enable", kParticipatingTools, tool_info.tool_name,
                   is_enabled ? tool_info.disable_args : tool_info.enable_args, analytics_list)
            << std::endl;
}

void ShowAlready(AnalyticsStatus status) {
  std::cout << fxl::Substitute(kMessageShowAlready,
                               status == AnalyticsStatus::kEnabled ? "enabled" : "disabled")
            << std::endl;
}

void ShowChangedTo(AnalyticsStatus status) {
  std::cout << fxl::Substitute(kMessageShowChangedTo,
                               status == AnalyticsStatus::kEnabled ? "enabled" : "disabled")
            << std::endl;
}

}  // namespace analytics::core_dev_tools::internal
