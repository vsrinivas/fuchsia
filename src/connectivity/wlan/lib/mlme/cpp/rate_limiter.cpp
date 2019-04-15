// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/rate_limiter.h>

namespace wlan {

RateLimiter::RateLimiter(zx::duration period, size_t max_events_per_period)
    : period_(period), max_events_per_period_(max_events_per_period) {}

bool RateLimiter::RecordEvent(zx::time now) {
  while (!events_.empty() && now >= events_.front() + period_) {
    events_.pop();
  }
  if (events_.size() >= max_events_per_period_) {
    return false;
  }
  events_.push(now);
  return true;
}

}  // namespace wlan
