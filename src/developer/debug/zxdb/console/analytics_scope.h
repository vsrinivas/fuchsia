// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_ANALYTICS_SCOPE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_ANALYTICS_SCOPE_H_

namespace zxdb {

// Manages static resources needed by analytics. Analytics related functions can be called as
// long as there is one object of this class alive. Example usage:
//
//     int main() {
//       AnalyticsScope _scope;
//       // Do other things...
//     }
class AnalyticsScope {
 public:
  AnalyticsScope();
  AnalyticsScope(const AnalyticsScope&) = delete;

  ~AnalyticsScope();

 private:
  inline static int object_count = 0;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_ANALYTICS_SCOPE_H_
