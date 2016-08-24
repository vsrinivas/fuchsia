// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_CPP_TIMELINE_H_
#define APPS_MEDIA_CPP_TIMELINE_H_

#include <chrono>  // NOLINT(build/c++11)
#include <stdint.h>

namespace mojo {
namespace media {

// Some helpful constants and static methods relating to timelines.
class Timeline {
 public:
  // Returns the current local time in nanoseconds since epoch.
  static int64_t local_now() {
    return std::chrono::steady_clock::now().time_since_epoch().count();
  }

  template <typename T>
  static constexpr int64_t ns_from_seconds(T seconds) {
    return static_cast<int64_t>(seconds * std::nano::den);
  }

  template <typename T>
  static constexpr int64_t ns_from_ms(T milliseconds) {
    return static_cast<int64_t>(milliseconds *
                                (std::nano::den / std::milli::den));
  }

  template <typename T>
  static constexpr int64_t ns_from_us(T microseconds) {
    return static_cast<int64_t>(microseconds *
                                (std::nano::den / std::micro::den));
  }
};

}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_CPP_TIMELINE_H_
