// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_TIME_TIME_POINT_H_
#define LIB_FTL_TIME_TIME_POINT_H_

#include <stdint.h>

#include "lib/ftl/time/time_delta.h"

namespace ftl {

// A TimePoint represents a point in time represented as an integer number of
// nanoseconds elapsed since an arbitrary point in the past.
class TimePoint {
 public:
  TimePoint() = default;
  static TimePoint Now();

  static constexpr TimePoint Max() {
    return TimePoint(std::numeric_limits<int64_t>::max());
  }

  // Compute the difference between two time points.
  TimeDelta operator-(TimePoint other) const {
    return TimeDelta::FromNanoseconds(ticks_ - other.ticks_);
  }

  TimePoint operator+(TimeDelta duration) const {
    return TimePoint(ticks_ + duration.ToNanoseconds());
  }
  TimePoint operator-(TimeDelta duration) const {
    return TimePoint(ticks_ - duration.ToNanoseconds());
  }

  bool operator==(TimePoint other) const { return ticks_ == other.ticks_; }
  bool operator!=(TimePoint other) const { return ticks_ != other.ticks_; }
  bool operator<(TimePoint other) const { return ticks_ < other.ticks_; }
  bool operator<=(TimePoint other) const { return ticks_ <= other.ticks_; }
  bool operator>(TimePoint other) const { return ticks_ > other.ticks_; }
  bool operator>=(TimePoint other) const { return ticks_ >= other.ticks_; }

 private:
  explicit constexpr TimePoint(int64_t ticks) : ticks_(ticks) {}

  int64_t ticks_ = 0;
};

}  // namespace ftl

#endif  // LIB_FTL_TIME_TIME_POINT_H_
