// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_GOOGLE_ANALYTICS_CLIENT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_GOOGLE_ANALYTICS_CLIENT_H_

#include "src/lib/analytics/cpp/google_analytics/client.h"

namespace zxdb {

// Forwarding types from analytics::google_analytics
using GoogleAnalyticsEvent = ::analytics::google_analytics::Event;
using GoogleAnalyticsNetError = ::analytics::google_analytics::NetError;
using GoogleAnalyticsNetErrorType = ::analytics::google_analytics::NetErrorType;

class GoogleAnalyticsClient : public analytics::google_analytics::Client {
 public:
  static void CurlGlobalInit();
  static void CurlGlobalCleanup();

 private:
  fit::promise<void, GoogleAnalyticsNetError> SendData(
      std::string_view user_agent,
      const std::map<std::string, std::string>& parameters) const override;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_GOOGLE_ANALYTICS_CLIENT_H_
