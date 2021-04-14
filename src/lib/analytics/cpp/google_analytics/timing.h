// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ANALYTICS_CPP_GOOGLE_ANALYTICS_TIMING_H_
#define SRC_LIB_ANALYTICS_CPP_GOOGLE_ANALYTICS_TIMING_H_

#include <optional>
#include <string_view>

#include "src/lib/analytics/cpp/google_analytics/hit.h"

namespace analytics::google_analytics {

// Representation of a Google Analytics timing hit.
// See
// https://developers.google.com/analytics/devguides/collection/protocol/v1/parameters#timing
class Timing : public Hit {
 public:
  Timing(std::string_view category, std::string_view variable, int64_t time,
         const std::optional<std::string_view>& label = std::nullopt);

  // Parameters that only supports the timing hit type
  void SetPageLoadTime(int64_t time);
  void SetDnsTime(int64_t time);
  void SetPageDownloadTime(int64_t time);
  void SetRedirectResponseTime(int64_t time);
  void SetTcpConnectTime(int64_t time);
  void SetServerResponseTime(int64_t time);
  void SetDomInteractiveTime(int64_t time);
  void SetContentLoadTime(int64_t time);
};

}  // namespace analytics::google_analytics

#endif  // SRC_LIB_ANALYTICS_CPP_GOOGLE_ANALYTICS_TIMING_H_
