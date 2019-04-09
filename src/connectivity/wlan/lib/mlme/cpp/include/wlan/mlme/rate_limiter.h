// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_RATE_LIMITER_H_
#define GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_RATE_LIMITER_H_

#include <lib/zx/time.h>
#include <queue>

namespace wlan {

class RateLimiter {
   public:
    RateLimiter(zx::duration period, size_t max_events_per_period);

    // If the event should be processed, record it and return true.
    // Otherwise, return false (if the maximum number of events for the period has been reached).
    //
    // For correct operation, timestamps in consecutive calls are expected to be non-decreasing
    // (i.e., to come from a monotonic clock.)
    bool RecordEvent(zx::time now);

   private:
    zx::duration period_;
    size_t max_events_per_period_;
    std::queue<zx::time> events_;
};

}  // namespace wlan

#endif  // GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_RATE_LIMITER_H_
