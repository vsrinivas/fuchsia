// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include "lib/fxl/time/time_delta.h"
#include "lib/fxl/time/time_point.h"

namespace fxl {

void PrintTo(const TimeDelta& time_delta, ::std::ostream* os) {
  *os << time_delta.ToNanoseconds();
}

void PrintTo(const TimePoint& time_point, ::std::ostream* os) {
  *os << (time_point - TimePoint()).ToNanoseconds();
}

}  // namespace fxl
