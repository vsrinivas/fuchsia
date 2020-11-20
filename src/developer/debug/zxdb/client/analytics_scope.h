// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_ANALYTICS_SCOPE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_ANALYTICS_SCOPE_H_

#include "src/developer/debug/zxdb/client/google_analytics_client.h"

namespace zxdb {

// Manages static resources needed by analytics. Analytics related functions can be called as
// long as there is one object of this class alive. The template parameter T should be a subclass of
// ::analytics::core_dev_tools::Analytics. Example usage:
//
//     int main() {
//       AnalyticsScope<Analytics> _scope;
//       // Do other things...
//     }
template <class T>
class AnalyticsScope {
 public:
  AnalyticsScope() {
    if (object_count == 0) {
      GoogleAnalyticsClient::CurlGlobalInit();
    }
    ++object_count;
  }
  AnalyticsScope(const AnalyticsScope&) = delete;

  ~AnalyticsScope() {
    --object_count;
    if (object_count == 0) {
      GoogleAnalyticsClient::CurlGlobalCleanup();
      T::CleanUpGoogleAnalyticsClient();
    }
  }

 private:
  inline static int object_count = 0;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_ANALYTICS_SCOPE_H_
