// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/timekeeper/clock_impl.h>

namespace timekeeper {

zx_status_t ClockImpl::GetTime(zx_clock_t clock_id, zx_time_t* time) const {
  return zx_clock_get_new(clock_id, time);
}

zx_time_t ClockImpl::GetMonotonicTime() const {
  return zx_clock_get_monotonic();
}

}  // namespace timekeeper
