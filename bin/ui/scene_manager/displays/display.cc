// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/displays/display.h"

#include <zircon/syscalls.h>

#include "lib/fxl/logging.h"

namespace scene_manager {

Display::Display(DisplayMetrics metrics)
    : last_vsync_time_(zx_time_get(ZX_CLOCK_MONOTONIC)), metrics_(metrics) {}

zx_time_t Display::GetLastVsyncTime() {
  zx_time_t current_time = zx_time_get(ZX_CLOCK_MONOTONIC);
  uint64_t num_elapsed_intervals =
      (current_time - last_vsync_time_) / kHardcodedPresentationIntervalNanos;
  uint64_t kMaxElapsedIntervals = 10000;
  if (num_elapsed_intervals > kMaxElapsedIntervals) {
    // A significant amount of time has elapsed since we were last provided with
    // a VSync time by the FrameScheduler, so don't assume we can accurately
    // compute the most recent Vsync.  Instead, pretend that a VSync just
    // happened; the FrameScheduler should quickly align us with reality.
    // TODO: log when this happens (at a higher verbosity setting)
    last_vsync_time_ = current_time;
    return last_vsync_time_;
  }

  return last_vsync_time_ +
         num_elapsed_intervals * kHardcodedPresentationIntervalNanos;
}

uint64_t Display::GetVsyncInterval() const {
  return kHardcodedPresentationIntervalNanos;
}

void Display::Claim() {
  FXL_DCHECK(!claimed_);
  claimed_ = true;
}

void Display::Unclaim() {
  FXL_DCHECK(claimed_);
  claimed_ = false;
}

}  // namespace scene_manager
