// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SYMBOL_INDEX_ANALYTICS_H_
#define TOOLS_SYMBOL_INDEX_ANALYTICS_H_

#include "src/lib/analytics/cpp/core_dev_tools/analytics.h"

namespace symbol_index {

class Analytics : public analytics::core_dev_tools::Analytics<Analytics> {
 public:
 private:
  friend class analytics::core_dev_tools::Analytics<Analytics>;

  static constexpr char kToolName[] = "symbol_index";
  static constexpr int64_t kQuitTimeoutMs = 500;
  static constexpr char kTrackingId[] = "UA-127897021-15";
  static constexpr char kEnableArgs[] = "--analytics=enable";
  static constexpr char kDisableArgs[] = "--analytics=disable";
  static constexpr char kStatusArgs[] = "--analytics-show";
  static constexpr char kAnalyticsList[] = R"(1. For invocation of symbol_index:
   - The version of symbol_index
   - The output of "uname -ms" (CPU architecture and kernel name)
2. Event of opting in/out of collection of analytics)";
};

}  // namespace symbol_index

#endif  // TOOLS_SYMBOL_INDEX_ANALYTICS_H_
