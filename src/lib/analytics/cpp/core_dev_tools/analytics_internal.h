// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_ANALYTICS_INTERNAL_H_
#define SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_ANALYTICS_INTERNAL_H_

#include <string_view>

#include "src/lib/analytics/cpp/google_analytics/client.h"

namespace analytics::core_dev_tools::internal {

void PrepareGoogleAnalyticsClient(google_analytics::Client& client, std::string_view tool_name,
                                  std::string_view tracking_id);

}  // namespace analytics::core_dev_tools::internal

#endif  // SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_ANALYTICS_INTERNAL_H_
