// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/time/stopwatch.h"

#include "lib/ftl/time/time_delta.h"
#include "lib/ftl/time/time_point.h"

namespace ftl {

void Stopwatch::Start() {
  start_time_ = TimePoint::Now();
}

TimeDelta Stopwatch::Elapsed() {
  return TimePoint::Now() - start_time_;
}

}  // namespace ftl
