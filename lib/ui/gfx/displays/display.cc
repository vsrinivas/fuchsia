// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/displays/display.h"

#include <zircon/syscalls.h>

#include "lib/fxl/logging.h"

namespace scenic {
namespace gfx {

Display::Display(uint64_t id, uint32_t width_in_px, uint32_t height_in_px)
    : last_vsync_time_(zx_clock_get(ZX_CLOCK_MONOTONIC)),
      display_id_(id),
      width_in_px_(width_in_px),
      height_in_px_(height_in_px) {
  zx::event::create(0, &ownership_event_);
}

zx_time_t Display::GetLastVsyncTime() {
  // Since listening for frame presentation events is our only way of knowing
  // when vsyncs have occurred, we often need to make an educated guess.
  const zx_time_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
  const zx_time_t interval_duration = GetVsyncInterval();
  const uint64_t num_intervals = (now - last_vsync_time_) / interval_duration;
  return last_vsync_time_ + (num_intervals * interval_duration);
}

zx_time_t Display::GetVsyncInterval() const {
  // TODO(MZ-124): We should derive an appropriate value from the rendering
  // targets, in particular giving priority to couple to the display refresh
  // (vsync).
  constexpr zx_time_t kHardcodedPresentationIntervalNanos = 16'666'667;
  return kHardcodedPresentationIntervalNanos;
}

void Display::set_last_vsync_time(zx_time_t vsync_time) {
  FXL_DCHECK(vsync_time >= last_vsync_time_ &&
             vsync_time <= zx_clock_get(ZX_CLOCK_MONOTONIC));
  last_vsync_time_ = vsync_time;
}

void Display::Claim() {
  FXL_DCHECK(!claimed_);
  claimed_ = true;
}

void Display::Unclaim() {
  FXL_DCHECK(claimed_);
  claimed_ = false;
}

}  // namespace gfx
}  // namespace scenic
