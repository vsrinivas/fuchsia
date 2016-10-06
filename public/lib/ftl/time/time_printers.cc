// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include "lib/ftl/time/time_delta.h"
#include "lib/ftl/time/time_point.h"

namespace ftl {

void PrintTo(const TimeDelta& time_delta, ::std::ostream* os) {
  *os << time_delta.ToNanoseconds();
}

void PrintTo(const TimePoint& time_point, ::std::ostream* os) {
  *os << (time_point - TimePoint()).ToNanoseconds();
}

}  // namespace ftl
