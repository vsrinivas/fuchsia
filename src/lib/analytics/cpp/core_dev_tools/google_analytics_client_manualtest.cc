// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <lib/fpromise/result.h>

#include <iostream>

#include "src/developer/debug/zxdb/common/curl.h"
#include "src/lib/analytics/cpp/core_dev_tools/google_analytics_client.h"

using ::analytics::core_dev_tools::GoogleAnalyticsClient;
using ::analytics::core_dev_tools::GoogleAnalyticsEvent;

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <tracking-id> <client-id>" << std::endl;
    return 1;
  }

  std::string tracking_id(argv[1]);
  std::string client_id(argv[1]);

  zxdb::Curl::GlobalInit();
  auto deferred_cleanup_curl = fit::defer(zxdb::Curl::GlobalCleanup);

  auto ga_client = GoogleAnalyticsClient(-1);
  ga_client.SetTrackingId(tracking_id);
  ga_client.SetClientId(client_id);
  ga_client.SetUserAgent("Fuchsia-tools-lib-analytics");

  auto event = GoogleAnalyticsEvent("test event", "test", "test label", 12345);

  ga_client.AddEvent(event);

  return 0;
}
