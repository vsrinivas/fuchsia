// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/core_dev_tools/analytics_internal.h"

#include "src/lib/analytics/cpp/core_dev_tools/general_parameters.h"
#include "src/lib/analytics/cpp/core_dev_tools/persistent_status.h"
#include "src/lib/analytics/cpp/core_dev_tools/user_agent.h"
#include "src/lib/fxl/strings/substitute.h"

namespace analytics::core_dev_tools::internal {

void PrepareGoogleAnalyticsClient(google_analytics::Client& client, std::string_view tool_name,
                                  std::string_view tracking_id, std::optional<BotInfo> bot) {
  client.SetUserAgent(GenerateUserAgent(tool_name));
  client.SetClientId(internal::PersistentStatus::GetUuid());
  client.SetTrackingId(tracking_id);

  if (bot.has_value()) {
    GeneralParameters parameters;
    if (bot->IsRunByBot()) {
      parameters.SetDataSource(fxl::Substitute("bot-$0", bot->name));
    } else {
      parameters.SetDataSource("user");
    }
    client.AddSharedParameters(parameters);
  }
}

}  // namespace analytics::core_dev_tools::internal
