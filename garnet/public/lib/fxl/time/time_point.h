// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_TIME_TIME_POINT_H_
#define LIB_FXL_TIME_TIME_POINT_H_

#include <stdint.h>

#include <iosfwd>

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/time/time_delta.h"

namespace fxl {

// A TimePoint represents a point in time represented as an integer number of
// nanoseconds elapsed since an arbitrary point in the past.
//
// WARNING: This class should not be serialized across reboots, or across
// devices: the reference point is only stable for a given device between
// reboots.
class FXL_EXPORT TimePoint {
 public:
  // Default TimePoint with internal value 0 (epoch).
  constexpr TimePoint() = default;

  static TimePoint Now();

  static constexpr TimePoint Min() {
    return TimePoint(std::numeric_limits<int64_t>::min());
  }

  static constexpr TimePoint Max() {
    return TimePoint(std::numeric_limits<int64_t>::max());
  }

  static constexpr TimePoint FromEpochDelta(TimeDelta ticks) {
    return TimePoint(ticks.ToNanoseconds());
  }

  TimeDelta ToEpochDelta() const { return TimeDelta::FromNanoseconds(ticks_); }

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

// Used to print useful values in gtest assertions. Should not be used in
// production code.
void PrintTo(const TimePoint& time_point, ::std::ostream* os);

}  // namespace fxl

#endif  // LIB_FXL_TIME_TIME_POINT_H_
