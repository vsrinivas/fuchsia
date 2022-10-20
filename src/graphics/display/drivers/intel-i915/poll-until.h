// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_POLL_UNTIL_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_POLL_UNTIL_H_

#include <lib/zx/time.h>
#include <zircon/assert.h>

namespace i915 {

// Polls a predicate periodically, until it becomes true or we time out.
//
// Returns true for success, meaning the predicate was true last time it was
// polled. Returns false for failure, meaning the predicate did not become true
// within the timeout.
//
// `poll_interval` is time interval between polls.  Popular values are zx::nsec(1)
// and zx::usec(1).
//
// `max_intervals` is the number of intervals to wait before timing out. If
// `predicate` is not true after this many intervals, the function returns
// false.
template <typename Lambda>
bool PollUntil(Lambda predicate, zx::duration poll_interval, int max_intervals) {
  ZX_ASSERT(max_intervals >= 0);

  for (int sleeps_left = max_intervals; sleeps_left > 0; --sleeps_left) {
    if (predicate())
      return true;
    zx::nanosleep(zx::deadline_after(poll_interval));
  }

  return predicate();
}

}  // namespace i915

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_POLL_UNTIL_H_
