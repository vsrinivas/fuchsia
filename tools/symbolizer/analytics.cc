// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/symbolizer/analytics.h"

namespace symbolizer {

void SymbolizationAnalyticsBuilder::SetAtLeastOneInvalidInput() {
  valid_ = true;
  at_least_one_invalid_input_ = true;
}

void SymbolizationAnalyticsBuilder::SetRemoteSymbolLookupEnabledBit(bool bit) {
  valid_ = true;
  remote_symbol_lookup_enabled_ = bit;
}

void SymbolizationAnalyticsBuilder::SetNumberOfModules(uint64_t count) {
  valid_ = true;
  number_of_modules_ = count;
}

void SymbolizationAnalyticsBuilder::SetNumberOfModulesWithLocalSymbols(uint64_t count) {
  valid_ = true;
  number_of_modules_with_local_symbols_ = count;
}

void SymbolizationAnalyticsBuilder::SetNumberOfModulesWithCachedSymbols(uint64_t count) {
  valid_ = true;
  number_of_modules_with_cached_symbols_ = count;
}

void SymbolizationAnalyticsBuilder::SetNumberOfModulesWithDownloadedSymbols(uint64_t count) {
  valid_ = true;
  number_of_modules_with_downloaded_symbols_ = count;
}

void SymbolizationAnalyticsBuilder::SetNumberOfModulesWithDownloadingFailure(uint64_t count) {
  valid_ = true;
  number_of_modules_with_downloading_failure_ = count;
}

void SymbolizationAnalyticsBuilder::IncreaseNumberOfFrames() {
  valid_ = true;
  number_of_frames_++;
}

void SymbolizationAnalyticsBuilder::IncreaseNumberOfFramesSymbolized() {
  valid_ = true;
  number_of_frames_symbolized_++;
}

void SymbolizationAnalyticsBuilder::IncreaseNumberOfFramesInvalid() {
  valid_ = true;
  number_of_frames_invalid_++;
}

void SymbolizationAnalyticsBuilder::TotalTimerStart() {
  total_timer_start_ = std::chrono::steady_clock::now();
}

void SymbolizationAnalyticsBuilder::TotalTimerStop() {
  valid_ = true;
  total_time = std::chrono::steady_clock::now() - total_timer_start_;
}

void SymbolizationAnalyticsBuilder::DownloadTimerStart() {
  download_timer_start_ = std::chrono::steady_clock::now();
}

void SymbolizationAnalyticsBuilder::DownloadTimerStop() {
  valid_ = true;
  download_time = std::chrono::steady_clock::now() - download_timer_start_;
}

analytics::google_analytics::Timing SymbolizationAnalyticsBuilder::build() {
  class AnalyticsGeneralParameters : public analytics::core_dev_tools::GeneralParameters {
   public:
    using analytics::core_dev_tools::GeneralParameters::SetCustomMetric;
  } parameters;

  auto total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_time).count();
  auto download_time_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(download_time).count();

  // t=timing
  // utc=symbolization
  // utv=<empty>
  // utt=<total wall time spent, in milliseconds>
  auto timing = analytics::google_analytics::Timing("symbolization", "", total_time_ms);

  // Custom parameters.
  // cm1=<1 if "at least one invalid input" else 0>
  parameters.SetCustomMetric(1, at_least_one_invalid_input_);
  // cm2=<# modules>
  parameters.SetCustomMetric(2, static_cast<int64_t>(number_of_modules_));
  // cm3=<# modules with local symbols>
  parameters.SetCustomMetric(3, static_cast<int64_t>(number_of_modules_with_local_symbols_));
  // cm4=<# modules with cached symbols>
  parameters.SetCustomMetric(4, static_cast<int64_t>(number_of_modules_with_cached_symbols_));
  // cm5=<# modules with downloaded symbols>
  parameters.SetCustomMetric(5, static_cast<int64_t>(number_of_modules_with_downloaded_symbols_));
  // cm6=<# modules with downloading failure>
  parameters.SetCustomMetric(6, static_cast<int64_t>(number_of_modules_with_downloading_failure_));
  // cm7=<# frames>
  parameters.SetCustomMetric(7, static_cast<int64_t>(number_of_frames_));
  // cm8=<# frames symbolized>
  parameters.SetCustomMetric(8, static_cast<int64_t>(number_of_frames_symbolized_));
  // cm9=<# frames out of valid modules>
  parameters.SetCustomMetric(9, static_cast<int64_t>(number_of_frames_invalid_));
  // cm10=<1 if "remote symbol lookup is enabled" else 0>
  parameters.SetCustomMetric(10, remote_symbol_lookup_enabled_);
  // cm11=<downloading time spent, in milliseconds>
  parameters.SetCustomMetric(11, download_time_ms);

  timing.AddGeneralParameters(parameters);

  return timing;
}

}  // namespace symbolizer
