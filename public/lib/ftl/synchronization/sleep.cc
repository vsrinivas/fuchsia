// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/synchronization/sleep.h"

#include <time.h>

namespace ftl {

void SleepFor(TimeDelta duration) {
  struct timespec ts = duration.ToTimespec();
  nanosleep(&ts, nullptr);
}

}  // namespace ftl
