// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/google_analytics/timing.h"

namespace analytics::google_analytics {

namespace {

constexpr char kHitType[] = "timing";
constexpr char kCategoryKey[] = "utc";
constexpr char kVariableKey[] = "utv";
constexpr char kTimeKey[] = "utt";
constexpr char kLabelKey[] = "utl";
constexpr char kPageLoadTimeKey[] = "plt";
constexpr char kDnsTimeKey[] = "dns";
constexpr char kPageDownloadTimeKey[] = "pdt";
constexpr char kRedirectResponseTimeKey[] = "rrt";
constexpr char kTcpConnectTimeKey[] = "tcp";
constexpr char kServerResponseTimeKey[] = "srt";
constexpr char kDomInteractiveTimeKey[] = "dit";
constexpr char kContentLoadTimeKey[] = "clt";

}  // namespace

Timing::Timing(std::string_view category, std::string_view variable, int64_t time,
               const std::optional<std::string_view>& label) {
  SetParameter(Hit::kHitTypeKey, kHitType);
  SetParameter(kCategoryKey, category);
  SetParameter(kVariableKey, variable);
  SetParameter(kTimeKey, std::to_string(time));
  if (label.has_value())
    SetParameter(kLabelKey, label.value());
}

void Timing::SetPageLoadTime(int64_t time) { SetParameter(kPageLoadTimeKey, std::to_string(time)); }

void Timing::SetDnsTime(int64_t time) { SetParameter(kDnsTimeKey, std::to_string(time)); }

void Timing::SetPageDownloadTime(int64_t time) {
  SetParameter(kPageDownloadTimeKey, std::to_string(time));
}

void Timing::SetRedirectResponseTime(int64_t time) {
  SetParameter(kRedirectResponseTimeKey, std::to_string(time));
}

void Timing::SetTcpConnectTime(int64_t time) {
  SetParameter(kTcpConnectTimeKey, std::to_string(time));
}

void Timing::SetServerResponseTime(int64_t time) {
  SetParameter(kServerResponseTimeKey, std::to_string(time));
}

void Timing::SetDomInteractiveTime(int64_t time) {
  SetParameter(kDomInteractiveTimeKey, std::to_string(time));
}

void Timing::SetContentLoadTime(int64_t time) {
  SetParameter(kContentLoadTimeKey, std::to_string(time));
}

}  // namespace analytics::google_analytics
