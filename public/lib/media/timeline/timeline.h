// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fxl/time/time_delta.h"
#include "lib/fxl/time/time_point.h"

namespace media {

// Some helpful constants and static methods relating to timelines.
class Timeline {
 public:
  // Returns the current local time in nanoseconds since epoch.
  static int64_t local_now() {
    return (fxl::TimePoint::Now() - fxl::TimePoint()).ToNanoseconds();
  }

  // Returns the specified time in nanoseconds since epoch.
  static int64_t local_time_from(fxl::TimePoint time_point) {
    return (time_point - fxl::TimePoint()).ToNanoseconds();
  }

  // Returns the specified time in nanoseconds from epoch as a |TimePoint|.
  static fxl::TimePoint to_time_point(int64_t nanosecond_from_epoch) {
    return fxl::TimePoint::FromEpochDelta(to_delta(nanosecond_from_epoch));
  }

  // Returns the specified delta in nanoseconds.
  static int64_t delta_from(fxl::TimeDelta time_delta) {
    return time_delta.ToNanoseconds();
  }

  // Returns the specified time delta in nanoseconds as a |TimeDelta|.
  static fxl::TimeDelta to_delta(int64_t nanosecond_from_epoch) {
    return fxl::TimeDelta::FromNanoseconds(nanosecond_from_epoch);
  }

  template <typename T>
  static constexpr int64_t ns_from_seconds(T seconds) {
    return fxl::TimeDelta::FromSeconds(seconds).ToNanoseconds();
  }

  template <typename T>
  static constexpr int64_t ns_from_ms(T milliseconds) {
    return fxl::TimeDelta::FromMilliseconds(milliseconds).ToNanoseconds();
  }

  template <typename T>
  static constexpr int64_t ns_from_us(T microseconds) {
    return fxl::TimeDelta::FromMicroseconds(microseconds).ToNanoseconds();
  }
};

}  // namespace media
