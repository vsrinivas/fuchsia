// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_WLAN_DRIVERS_INTERNAL_THROTTLE_COUNTER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_WLAN_DRIVERS_INTERNAL_THROTTLE_COUNTER_H_

#include <atomic>
#include <cstdint>

namespace wlan::drivers {

template <typename Throttler>
class ThrottleCounter {
 public:
  explicit ThrottleCounter(Throttler& throttler) : throttler_(throttler) {}

  // Attempt to consume a token from the token bucket to use for logging. If the consume is
  // successful the previous number of throttled events is placed in |out_counter|. If the consume
  // is not successful the number of throttled events, INCLUDING this one, is placed in
  // |out_counter|. The internal counter is reset on each successful consume so the next call to
  // consume will either set |out_counter| to 0 (on success) or 1 (on failure).
  bool consume(uint64_t* out_counter) {
    if (throttler_.consume()) {
      // Clear the counter and fetch the previous value atomically, this ensures that in the case
      // of multiple consumes in parallel only one of them will report multiple throttled events.
      *out_counter = counter_.exchange(0, std::memory_order_relaxed);
      return true;
    }
    // fetch_add returns the old value so add 1
    *out_counter = counter_.fetch_add(1, std::memory_order_relaxed) + 1;
    return false;
  }

 private:
  Throttler& throttler_;
  std::atomic<uint64_t> counter_ = 0;
};

}  // namespace wlan::drivers

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_WLAN_DRIVERS_INTERNAL_THROTTLE_COUNTER_H_
