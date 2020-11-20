// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_ANALYTICS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_ANALYTICS_H_

#include "src/developer/debug/zxdb/client/analytics_scope.h"
#include "src/developer/debug/zxdb/client/google_analytics_client.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/lib/analytics/cpp/core_dev_tools/analytics.h"

namespace zxdb {

class Analytics : public analytics::core_dev_tools::Analytics<Analytics> {
 public:
  static void Init(Session& session, analytics::core_dev_tools::SubLaunchStatus sub_launch_status);
  static void IfEnabledSendInvokeEvent(Session* session);

 private:
  friend class analytics::core_dev_tools::Analytics<Analytics>;
  friend class AnalyticsScope<Analytics>;

  // Move base class Init() to private. Users of this class can only call Init(Session& session).
  using analytics::core_dev_tools::Analytics<Analytics>::Init;

  static constexpr char kToolName[] = "zxdb";
  static constexpr char kTrackingId[] = "UA-127897021-11";
  static constexpr char kEnableArgs[] = "--analytics=enable";
  static constexpr char kDisableArgs[] = "--analytics=disable";
  static constexpr char kStatusArgs[] = "--show-analytics";
  static constexpr char kAnalyticsList[] = R"(1. For invocation of zxdb:
   - The version of zxdb
   - The output of "uname -ms" (CPU architecture and kernel name)
2. Event of opting in/out of collection of analytics)";

  static void SetRuntimeAnalyticsStatus(analytics::core_dev_tools::AnalyticsStatus status) {
    should_be_enabled_runtime_ = (status == analytics::core_dev_tools::AnalyticsStatus::kEnabled);
  }

  static std::unique_ptr<analytics::google_analytics::Client> CreateGoogleAnalyticsClient() {
    return std::make_unique<GoogleAnalyticsClient>();
  }

  static void RunTask(fit::pending_task task);

  static bool IsEnabled(Session* session);

  static bool should_be_enabled_runtime_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_ANALYTICS_H_
