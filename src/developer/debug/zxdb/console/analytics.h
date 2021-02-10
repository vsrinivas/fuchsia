// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_ANALYTICS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_ANALYTICS_H_

#include "src/developer/debug/zxdb/client/session.h"
#include "src/lib/analytics/cpp/core_dev_tools/analytics.h"

namespace zxdb {

class Analytics : public analytics::core_dev_tools::Analytics<Analytics> {
 public:
  static void Init(Session& session, analytics::core_dev_tools::AnalyticsOption analytics_option);
  static void IfEnabledSendInvokeEvent(Session* session);

 private:
  friend class analytics::core_dev_tools::Analytics<Analytics>;

  // Move some base class methods to private. Users of this class need to call "overloaded"
  // version of these functions that take a session as an argument.
  using analytics::core_dev_tools::Analytics<Analytics>::Init;
  using analytics::core_dev_tools::Analytics<Analytics>::IfEnabledSendInvokeEvent;

  static constexpr char kToolName[] = "zxdb";
  static constexpr int64_t kQuitTimeoutMs = 500;
  static constexpr char kTrackingId[] = "UA-127897021-11";
  static constexpr char kEnableArgs[] = "--analytics=enable";
  static constexpr char kDisableArgs[] = "--analytics=disable";
  static constexpr char kStatusArgs[] = "--analytics-show";
  static constexpr char kAnalyticsList[] = R"(1. For invocation of zxdb:
   - The version of zxdb
   - The output of "uname -ms" (CPU architecture and kernel name)
2. Event of opting in/out of collection of analytics)";

  static bool IsEnabled(Session* session);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_ANALYTICS_H_
