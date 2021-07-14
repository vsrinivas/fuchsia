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

class SymbolizationAnalyticsBuilder {
 public:
  SymbolizationAnalyticsBuilder() = default;
  bool valid() const { return valid_; }

  void SetAtLeastOneInvalidInput();
  void SetRemoteSymbolLookupEnabledBit(bool bit);
  void SetNumberOfModules(uint64_t count);
  void SetNumberOfModulesWithLocalSymbols(uint64_t count);
  void SetNumberOfModulesWithCachedSymbols(uint64_t count);
  void SetNumberOfModulesWithDownloadedSymbols(uint64_t count);
  void SetNumberOfModulesWithDownloadingFailure(uint64_t count);
  void IncreaseNumberOfFrames();
  void IncreaseNumberOfFramesSymbolized();
  void IncreaseNumberOfFramesInvalid();

  // Timing related.
  void TotalTimerStart();
  void TotalTimerStop();
  void DownloadTimerStart();
  void DownloadTimerStop();

  // build the timing hit
  analytics::google_analytics::Timing build();

 private:
  bool valid_ = false;
  bool remote_symbol_lookup_enabled_ = false;
  bool at_least_one_invalid_input_ = false;
  uint64_t number_of_modules_ = 0;
  uint64_t number_of_modules_with_local_symbols_ = 0;
  uint64_t number_of_modules_with_cached_symbols_ = 0;
  uint64_t number_of_modules_with_downloaded_symbols_ = 0;
  uint64_t number_of_modules_with_downloading_failure_ = 0;
  uint64_t number_of_frames_ = 0;
  uint64_t number_of_frames_symbolized_ = 0;
  uint64_t number_of_frames_invalid_ = 0;

  std::chrono::time_point<std::chrono::steady_clock> total_timer_start_;
  std::chrono::steady_clock::duration total_time{};
  std::chrono::time_point<std::chrono::steady_clock> download_timer_start_;
  std::chrono::steady_clock::duration download_time{};
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
   - The version of symbolizer.
   - The output of "uname -ms" (CPU architecture and kernel name).
2. Event of opting in/out of collection of analytics.
3. For each hit sent to Google Analytics, we also collect:
   - Whether symbolizer is run in a bot environment and if so the name of the
     bot (e.g. LUCI, Travis, etc.).
4. For each stack trace:
   - Whether there is at least one invalid input.
   - Number of modules.
   - Number of modules with local symbols, i.e. binaries and symbol files are
     built locally.
   - Number of modules with remote symbols, i.e. the build ID can be found on
     the symbol server.
   - Number of modules with cached symbols.
   - Number of modules with downloaded symbols.
   - Number of modules with downloading-failed symbols.
   - Number of frames.
   - Number of frames not valid, i.e. out of valid modules.
   - Number of frames symbolized.
   - Whether remote symbol lookup is enabled.
   - Total wall time spent.
   - Downloading time spent.)";
};

}  // namespace symbolizer

#endif  // TOOLS_SYMBOLIZER_ANALYTICS_H_
