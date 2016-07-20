// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/time/time_point.h"

#include <time.h>

#include "lib/ftl/logging.h"

namespace ftl {

// static
TimePoint TimePoint::Now() {
  struct timespec ts;
  int res = clock_gettime(CLOCK_MONOTONIC, &ts);
  FTL_DCHECK(res == 0);
  (void)res;
  return TimePoint() + TimeDelta::FromTimespec(ts);
}

}  // namespace ftl
