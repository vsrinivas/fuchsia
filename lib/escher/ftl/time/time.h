// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FTL_TIME_TIME_H_
#define FTL_TIME_TIME_H_

#include <chrono>

namespace ftl {

typedef std::chrono::steady_clock::time_point TimePoint;
typedef std::chrono::steady_clock::duration Duration;

TimePoint Now();

}  // namespace ftl

#endif  // FTL_TIME_TIME_H_
