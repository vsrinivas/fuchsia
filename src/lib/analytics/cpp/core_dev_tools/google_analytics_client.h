// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_GOOGLE_ANALYTICS_CLIENT_H_
#define SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_GOOGLE_ANALYTICS_CLIENT_H_

#include "src/lib/analytics/cpp/core_dev_tools/analytics_executor.h"
#include "src/lib/analytics/cpp/google_analytics/client.h"
#include "src/lib/analytics/cpp/google_analytics/event.h"
#include "src/lib/analytics/cpp/google_analytics/timing.h"

namespace analytics::core_dev_tools {

// Forwarding types from analytics::google_analytics
using GoogleAnalyticsEvent = ::analytics::google_analytics::Event;
using GoogleAnalyticsTiming = ::analytics::google_analytics::Timing;

// To use this client, one needs to (if not already) add the following lines to the main()
// function before any threads are spawned and any use of Curl or this client:
//     debug_ipc::Curl::GlobalInit();
//     auto deferred_cleanup_curl = fit::defer(debug_ipc::Curl::GlobalCleanup);
// and include related headers, e.g. <lib/fit/defer.h> and "src/developer/debug/zxdb/common/curl.h".
class GoogleAnalyticsClient : public google_analytics::Client {
 public:
  explicit GoogleAnalyticsClient(int64_t quit_timeout_ms) : executor_(quit_timeout_ms) {}
  GoogleAnalyticsClient() : GoogleAnalyticsClient(0) {}

 private:
  void SendData(std::string_view user_agent,
                std::map<std::string, std::string> parameters) override;
  AnalyticsExecutor executor_;
};

}  // namespace analytics::core_dev_tools

#endif  // SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_GOOGLE_ANALYTICS_CLIENT_H_
