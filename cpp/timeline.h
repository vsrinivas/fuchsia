// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_CPP_TIMELINE_H_
#define APPS_MEDIA_CPP_TIMELINE_H_

#include "lib/ftl/time/time_delta.h"
#include "lib/ftl/time/time_point.h"

namespace mojo {
namespace media {

// Some helpful constants and static methods relating to timelines.
class Timeline {
 public:
  // Returns the current local time in nanoseconds since epoch.
  static int64_t local_now() {
    return (ftl::TimePoint::Now() - ftl::TimePoint()).ToNanoseconds();
  }

  template <typename T>
  static constexpr int64_t ns_from_seconds(T seconds) {
    return ftl::TimeDelta::FromSeconds(seconds).ToNanoseconds();
  }

  template <typename T>
  static constexpr int64_t ns_from_ms(T milliseconds) {
    return ftl::TimeDelta::FromMilliseconds(milliseconds).ToNanoseconds();
  }

  template <typename T>
  static constexpr int64_t ns_from_us(T microseconds) {
    return ftl::TimeDelta::FromMicroseconds(microseconds).ToNanoseconds();
  }
};

}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_CPP_TIMELINE_H_
