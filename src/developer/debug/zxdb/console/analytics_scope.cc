// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/analytics_scope.h"

#include "src/developer/debug/zxdb/console/analytics.h"
#include "src/developer/debug/zxdb/console/google_analytics_client.h"

namespace zxdb {

AnalyticsScope::AnalyticsScope() {
  if (object_count == 0) {
    GoogleAnalyticsClient::CurlGlobalInit();
  }
  ++object_count;
}

AnalyticsScope::~AnalyticsScope() {
  --object_count;
  if (object_count == 0) {
    GoogleAnalyticsClient::CurlGlobalCleanup();
    Analytics::CleanUpGoogleAnalyticsClient();
  }
}

}  // namespace zxdb
