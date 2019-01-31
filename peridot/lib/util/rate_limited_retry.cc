// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/util/rate_limited_retry.h"

namespace modular {

RateLimitedRetry::RateLimitedRetry(const Threshold& threshold)
    : threshold_(threshold), failure_series_count_(0) {}

bool RateLimitedRetry::ShouldRetry() {
  zx::time now = zx::clock::get_monotonic();
  if (failure_series_count_ == 0 ||
      now - failure_series_start_ >= threshold_.period) {
    failure_series_start_ = now;
    failure_series_count_ = 0;
  }

  if (failure_series_count_ >= threshold_.count) {
    return false;
  } else {
    ++failure_series_count_;
    return true;
  }
}

}  // namespace modular
