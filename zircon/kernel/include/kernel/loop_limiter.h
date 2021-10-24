// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_LOOP_LIMITER_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_LOOP_LIMITER_H_

#include <lib/affine/ratio.h>
#include <platform.h>
#include <zircon/types.h>

#include <cstdint>

#include <ktl/move.h>

// LoopLimiter is used to detect when a thread is looping for "too long".
//
// Example usage:
//
//   // Make sure we spend no more than 30,000 nanoseconds in the loop.
//   auto limiter = LoopLimiter<>::WithDuration(30000);
//   while (!limiter.Exceeded()) {
//     ...
//   }
//
// Because getting the current ticks may be expensive in some virtualized
// environments, the template parameter |ItersPerGetTicks| controls how often
// |current_ticks| is called.  For example:
//
//   // Make sure we spend no more than 30,000 nanoseconds in the loop, but
//   // don't call current_ticks() more than once every 1,000 loop iterations.
//   auto limiter = LoopLimiter<1000>::WithDuration(30000);
//   while (!limiter.Exceeded()) {
//     ...
//   }
//
// An |ItersPerGetTicks| value of 1 means call |current_ticks| for each
// invocation of |Exceeded|.
template <uint64_t ItersPerGetTicks>
class LoopLimiter {
 public:
  static_assert(ItersPerGetTicks > 0);

  // Construct a limiter with a relative timeout.
  //
  // If |duration| is <= 0 |Exceeded| will always return false.
  static LoopLimiter<ItersPerGetTicks> WithDuration(zx_duration_t duration) {
    const zx_ticks_t relative_ticks = platform_get_ticks_to_time_ratio().Inverse().Scale(duration);
    return ktl::move(LoopLimiter<ItersPerGetTicks>(relative_ticks));
  }

  // Returns true if the timeout has been exceeded.
  //
  // Call once per loop iteration.
  bool Exceeded() {
    if constexpr (ItersPerGetTicks <= 1) {
      const zx_ticks_t now = current_ticks();
      return now >= deadline_ticks_;
    }

    ++iter_since_;
    if (iter_since_ >= ItersPerGetTicks) {
      iter_since_ = 0;
      const zx_ticks_t now = current_ticks();
      return now >= deadline_ticks_;
    }
    return false;
  }

 private:
  explicit LoopLimiter(zx_ticks_t relative_ticks) {
    if (relative_ticks > 0) {
      const zx_ticks_t now = current_ticks();
      deadline_ticks_ = zx_ticks_add_ticks(now, relative_ticks);
    }
  }

  // Absolute deadline measured in monotonic clock ticks.
  zx_ticks_t deadline_ticks_{};
  // Number of iterations since the last call to |current_ticks|.
  uint64_t iter_since_{};
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_LOOP_LIMITER_H_
