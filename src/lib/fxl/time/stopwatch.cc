// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fxl/time/stopwatch.h"

#include "src/lib/fxl/time/time_delta.h"
#include "src/lib/fxl/time/time_point.h"

namespace fxl {

void Stopwatch::Start() { start_time_ = TimePoint::Now(); }

TimeDelta Stopwatch::Elapsed() { return TimePoint::Now() - start_time_; }

}  // namespace fxl
