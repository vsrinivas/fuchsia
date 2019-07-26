// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cobalt-client/cpp/histogram.h>
#include <lib/fzl/time.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>

namespace cobalt_client {
namespace internal {

template <typename Clock>
class TimerBase {
 public:
  explicit TimerBase(bool is_collecting) : start_(is_collecting ? Clock::now().get() : 0) {}
  TimerBase(const TimerBase&) = delete;
  TimerBase(TimerBase&& other) : start_(other.start_) { other.start_ = zx::ticks(0); }
  TimerBase& operator=(const TimerBase&) = delete;
  TimerBase& operator=(TimerBase&& other) = delete;
  ~TimerBase() = default;

  // Returns the duration since creation. If |is_collecting| is false, will return
  // 0.
  zx::duration End() {
    if (start_.get() == 0) {
      return zx::duration(0);
    }
    return fzl::TicksToNs(Clock::now() - start_);
  }

  // Resets the timer. If |is_collecting| is false, has no effect.
  void Reset() {
    if (start_.get() == 0) {
      return;
    }
    start_ = Clock::now();
  }

 private:
  zx::ticks start_;
};
}  // namespace internal

// Utility class for measuring the amount of ticks in an interval.
//
// This class is moveable, but not copyable or assignable.
using Timer = internal::TimerBase<zx::ticks>;

}  // namespace cobalt_client
