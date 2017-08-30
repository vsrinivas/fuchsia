// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/synchronization/sleep.h"

#include <chrono>
#include <thread>

namespace ftl {

void SleepFor(TimeDelta duration) {
  std::this_thread::sleep_for(std::chrono::nanoseconds(duration.ToNanoseconds()));
}

}  // namespace ftl
