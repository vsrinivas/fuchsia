// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_H_
#define GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_H_

#include <zircon/types.h>
#include <cstdint>

#include "lib/fxl/macros.h"
#include "lib/zx/event.h"

namespace scenic {
namespace gfx {

// Display is a placeholder that provides make-believe values for screen
// resolution, vsync interval, last vsync time, etc.
class Display {
 public:
  Display(uint64_t id, uint32_t width_in_px, uint32_t height_in_px);

  // Obtain the time of the last Vsync, in nanoseconds.
  zx_time_t GetLastVsyncTime();

  // Obtain the interval between Vsyncs, in nanoseconds.
  zx_time_t GetVsyncInterval() const;

  // Claiming a display means that no other display renderer can use it.
  bool is_claimed() const { return claimed_; }
  void Claim();
  void Unclaim();

  // The display's ID in the context of the DisplayManager's DisplayController.
  uint64_t display_id() { return display_id_; };
  uint32_t width_in_px() { return width_in_px_; };
  uint32_t height_in_px() { return height_in_px_; };

  // Event signaled by DisplayManager when ownership of the display
  // changes. This event backs Scenic's GetOwnershipEvent API.
  const zx::event& ownership_event() { return ownership_event_; };

 private:
  // Temporary friendship to allow FrameScheduler to feed back the Vsync timings
  // gleaned from EventTimestamper.  This should go away once we receive real
  // VSync times from the display driver.
  friend class FrameScheduler;
  void set_last_vsync_time(zx_time_t vsync_time);

  zx_time_t last_vsync_time_;
  const uint64_t display_id_;
  const uint32_t width_in_px_;
  const uint32_t height_in_px_;
  zx::event ownership_event_;

  bool claimed_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(Display);
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_H_
