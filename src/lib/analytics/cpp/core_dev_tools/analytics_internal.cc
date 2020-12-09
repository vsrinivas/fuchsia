// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/core_dev_tools/analytics_internal.h"

#include "src/lib/analytics/cpp/core_dev_tools/persistent_status.h"
#include "src/lib/analytics/cpp/core_dev_tools/user_agent.h"

namespace analytics::core_dev_tools::internal {

void PrepareGoogleAnalyticsClient(google_analytics::Client& client, std::string_view tool_name,
                                  std::string_view tracking_id) {
  client.SetUserAgent(GenerateUserAgent(tool_name));
  client.SetClientId(internal::PersistentStatus::GetUuid());
  client.SetTrackingId(tracking_id);
}

}  // namespace analytics::core_dev_tools::internal
