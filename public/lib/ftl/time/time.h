// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_TIME_TIME_H_
#define LIB_FTL_TIME_TIME_H_

#include <chrono>

namespace ftl {

typedef std::chrono::nanoseconds Duration;
typedef std::chrono::time_point<std::chrono::steady_clock, Duration> TimePoint;

TimePoint Now();

}  // namespace ftl

#endif  // LIB_FTL_TIME_TIME_H_
