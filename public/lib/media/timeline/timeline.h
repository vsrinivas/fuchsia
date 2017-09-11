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
