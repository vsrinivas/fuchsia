// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_EVENT_LIMITER_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_EVENT_LIMITER_H_

#include <platform.h>
#include <zircon/types.h>

#include <ktl/atomic.h>

// An EventLimiter allows an action to only be taken every K seconds in a thread-safe manner.
//
// Use as follows:
//
//   EventLimiter<ZX_SEC(1)> limiter;
//   while (...) {
//     if (limiter.Ready()) {
//       printf("...");
//     }
//     // ...
//   }
//
template <zx_duration_t Duration>
class EventLimiter {
 public:
  bool Ready() {
    zx_time_t now = current_time();

    // If we have recently taken action, we don't need to do it again.
    zx_time_t last_event = last_event_.load(ktl::memory_order_relaxed);
    if (last_event != 0 && now < last_event + Duration) {
      return false;
    }

    // Otherwise, record that we have acted. If we race with another thread, assume it has taken
    // action and we don't need to.
    if (!last_event_.compare_exchange_strong(last_event, now, ktl::memory_order_relaxed)) {
      return false;
    }

    return true;
  }

 private:
  ktl::atomic<zx_time_t> last_event_ = 0;
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_EVENT_LIMITER_H_
