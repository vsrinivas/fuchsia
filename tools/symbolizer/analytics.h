// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SYMBOLIZER_ANALYTICS_H_
#define TOOLS_SYMBOLIZER_ANALYTICS_H_

#include <chrono>

#include "src/lib/analytics/cpp/core_dev_tools/analytics.h"
#include "src/lib/analytics/cpp/core_dev_tools/general_parameters.h"
#include "src/lib/analytics/cpp/google_analytics/timing.h"
#include "src/lib/fxl/strings/substitute.h"

namespace symbolizer {

class StackTraceHitBuilder {
 public:
  StackTraceHitBuilder() = default;
  void SetAtLeastOneInvalidInputBit(bool bit);
  void SetRemoteSymbolLookupEnabledBit(bool bit);
  void SetNumberOfModules(int64_t count);
  void SetNumberOfModulesWithLocalSymbols(int64_t count);
  void SetNumberOfModulesWithCachedSymbols(int64_t count);
  void SetNumberOfModulesWithDownloadedSymbols(int64_t count);
  void SetNumberOfModulesWithDownloadingFailure(int64_t count);
  void SetNumberOfFrames(int64_t count);
  void SetNumberOfFramesSymbolized(int64_t count);
  void SetNumberOfFramesInvalid(int64_t count);

  // timing related
  void TotalTimerStart();
  void TotalTimerStop();
  void DownloadTimerStart();
  void DownloadTimerStop();

  // build the timing hit
  analytics::google_analytics::Timing build();

 private:
  // Hides SetCustomMetric() from public access, but allows it being called by methods in this
  // class. Users need to call the Set... functions above.
  class AnalyticsGeneralParameters : public analytics::core_dev_tools::GeneralParameters {
   public:
    using ::analytics::core_dev_tools::GeneralParameters::SetCustomMetric;
  };
  AnalyticsGeneralParameters parameters_;
  std::chrono::time_point<std::chrono::steady_clock> total_timer_start_;
  std::chrono::steady_clock::duration total_time;
  std::chrono::time_point<std::chrono::steady_clock> download_timer_start_;
  std::chrono::steady_clock::duration download_time;
};

class Analytics : public analytics::core_dev_tools::Analytics<Analytics> {
 private:
  friend class analytics::core_dev_tools::Analytics<Analytics>;

  static constexpr char kToolName[] = "symbolizer";
  static constexpr int64_t kQuitTimeoutMs = 500;
  static constexpr char kTrackingId[] = "UA-127897021-14";
  static constexpr char kEnableArgs[] = "--analytics=enable";
  static constexpr char kDisableArgs[] = "--analytics=disable";
  static constexpr char kStatusArgs[] = "--analytics-show";
  static constexpr char kAnalyticsList[] = R"(1. For invocation of symbolizer:
   - The version of symbolizer
   - The output of "uname -ms" (CPU architecture and kernel name)
2. Event of opting in/out of collection of analytics
3. For each hit sent to Google Analytics, we also collect:
   - whether symbolizer is run in a bot environment and if so the name of the
     bot (e.g. LUCI, Travis, etc.))";
};

}  // namespace symbolizer

#endif  // TOOLS_SYMBOLIZER_ANALYTICS_H_
