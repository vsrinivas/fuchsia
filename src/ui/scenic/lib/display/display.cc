// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/display.h"

#include <zircon/syscalls.h>

#include <trace/event.h>

#include "src/lib/fxl/logging.h"

namespace scenic_impl {
namespace display {

Display::Display(uint64_t id, uint32_t width_in_px, uint32_t height_in_px,
                 std::vector<zx_pixel_format_t> pixel_formats)
    : vsync_timing_(std::make_shared<scheduling::VsyncTiming>()),
      display_id_(id),
      width_in_px_(width_in_px),
      height_in_px_(height_in_px),
      pixel_formats_(pixel_formats) {
  zx::event::create(0, &ownership_event_);
}
Display::Display(uint64_t id, uint32_t width_in_px, uint32_t height_in_px)
    : Display(id, width_in_px, height_in_px, {ZX_PIXEL_FORMAT_ARGB_8888}) {}

void Display::Claim() {
  FXL_DCHECK(!claimed_);
  claimed_ = true;
}

void Display::Unclaim() {
  FXL_DCHECK(claimed_);
  claimed_ = false;
}

void Display::OnVsync(zx::time timestamp) {
  zx::duration time_since_last_vsync = timestamp - vsync_timing_->last_vsync_time();

  if (vsync_timing_->last_vsync_time() != zx::time(0)) {
    // Estimate current vsync interval. Need to include a maximum to mitigate any
    // potential issues during long breaks.
    if (time_since_last_vsync >= kMaximumVsyncInterval) {
      FXL_LOG(WARNING) << "More than " << kMaximumVsyncInterval.to_msecs()
                       << "ms observed between vsyncs.";
    }
    vsync_timing_->set_vsync_interval(time_since_last_vsync < kMaximumVsyncInterval
                                          ? time_since_last_vsync
                                          : vsync_timing_->vsync_interval());
  }

  vsync_timing_->set_last_vsync_time(timestamp);

  TRACE_INSTANT("gfx", "Display::OnVsync", TRACE_SCOPE_PROCESS, "Timestamp", timestamp.get(),
                "Vsync interval", vsync_timing_->vsync_interval().get());
}

}  // namespace display
}  // namespace scenic_impl
