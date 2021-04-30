// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/symbolizer/analytics.h"

namespace symbolizer {

namespace {

constexpr int kIndexAtLeastOneInvalidInputBit = 1;
constexpr int kIndexRemoteSymbolLookupEnabledBit = 10;
constexpr int kIndexNumberOfModules = 2;
constexpr int kIndexNumberOfModulesWithLocalSymbols = 3;
constexpr int kIndexNumberOfModulesWithCachedSymbols = 4;
constexpr int kIndexNumberOfModulesWithDownloadedSymbols = 5;
constexpr int kIndexNumberOfModulesWithDownloadingFailure = 6;
constexpr int kIndexNumberOfFrames = 7;
constexpr int kIndexNumberOfFramesSymbolized = 8;
constexpr int kIndexNumberOfFramesInvalid = 9;
constexpr char kTimingCategory[] = "symbolization";
constexpr char kTimingVariable[] = "";

}  // namespace

using ::analytics::google_analytics::Timing;

void StackTraceHitBuilder::SetAtLeastOneInvalidInputBit(bool bit) {
  parameters_.SetCustomMetric(kIndexAtLeastOneInvalidInputBit, bit);
}

void StackTraceHitBuilder::SetRemoteSymbolLookupEnabledBit(bool bit) {
  parameters_.SetCustomMetric(kIndexRemoteSymbolLookupEnabledBit, bit);
}

void StackTraceHitBuilder::SetNumberOfModules(int64_t count) {
  parameters_.SetCustomMetric(kIndexNumberOfModules, count);
}

void StackTraceHitBuilder::SetNumberOfModulesWithLocalSymbols(int64_t count) {
  parameters_.SetCustomMetric(kIndexNumberOfModulesWithLocalSymbols, count);
}

void StackTraceHitBuilder::SetNumberOfModulesWithCachedSymbols(int64_t count) {
  parameters_.SetCustomMetric(kIndexNumberOfModulesWithCachedSymbols, count);
}

void StackTraceHitBuilder::SetNumberOfModulesWithDownloadedSymbols(int64_t count) {
  parameters_.SetCustomMetric(kIndexNumberOfModulesWithDownloadedSymbols, count);
}

void StackTraceHitBuilder::SetNumberOfModulesWithDownloadingFailure(int64_t count) {
  parameters_.SetCustomMetric(kIndexNumberOfModulesWithDownloadingFailure, count);
}

void StackTraceHitBuilder::SetNumberOfFrames(int64_t count) {
  parameters_.SetCustomMetric(kIndexNumberOfFrames, count);
}

void StackTraceHitBuilder::SetNumberOfFramesSymbolized(int64_t count) {
  parameters_.SetCustomMetric(kIndexNumberOfFramesSymbolized, count);
}

void StackTraceHitBuilder::SetNumberOfFramesInvalid(int64_t count) {
  parameters_.SetCustomMetric(kIndexNumberOfFramesInvalid, count);
}

void StackTraceHitBuilder::TotalTimerStart() {
  total_timer_start_ = std::chrono::steady_clock::now();
}

void StackTraceHitBuilder::TotalTimerStop() {
  total_time = std::chrono::steady_clock::now() - total_timer_start_;
}

void StackTraceHitBuilder::DownloadTimerStart() {
  download_timer_start_ = std::chrono::steady_clock::now();
}

void StackTraceHitBuilder::DownloadTimerStop() {
  download_time = std::chrono::steady_clock::now() - download_timer_start_;
}

Timing StackTraceHitBuilder::build() {
  auto total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_time).count();
  auto download_time_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(download_time).count();
  auto timing = Timing(kTimingCategory, kTimingVariable, total_time_ms);
  timing.SetPageLoadTime(total_time_ms);
  timing.SetPageDownloadTime(download_time_ms);
  timing.AddGeneralParameters(parameters_);
  return timing;
}

}  // namespace symbolizer
