// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_TIMEKEEPER_CLOCK_IMPL_H_
#define LIB_TIMEKEEPER_CLOCK_IMPL_H_

#include <lib/timekeeper/system_clock.h>

namespace timekeeper {
// Deprecated class. It must be replaced by SystemClock.
using ClockImpl = SystemClock;
}  // namespace timekeeper

#endif  // LIB_TIMEKEEPER_CLOCK_IMPL_H_
