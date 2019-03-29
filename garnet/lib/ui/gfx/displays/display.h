// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_H_
#define GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_H_

#include <zircon/types.h>
#include <cstdint>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "lib/zx/event.h"
#include "zircon/pixelformat.h"

namespace scenic_impl {
namespace gfx {

// Display is a placeholder that provides make-believe values for screen
// resolution, vsync interval, last vsync time, etc.
class Display {
 public:
  Display(uint64_t id, uint32_t width_in_px, uint32_t height_in_px,
          std::vector<zx_pixel_format_t> pixel_formats);
  Display(uint64_t id, uint32_t width_in_px, uint32_t height_in_px);
  virtual ~Display() = default;

  // Should be registered by DisplayCompositor to be called on every received
  // vsync signal.
  void OnVsync(zx_time_t timestamp);

  // Obtain the time of the last Vsync, in nanoseconds.
  zx_time_t GetLastVsyncTime() const { return last_vsync_time_; }

  // Obtain the interval between Vsyncs, in nanoseconds.
  zx_duration_t GetVsyncInterval() const { return vsync_interval_; };

  // Claiming a display means that no other display renderer can use it.
  bool is_claimed() const { return claimed_; }
  void Claim();
  void Unclaim();

  // The display's ID in the context of the DisplayManager's DisplayController.
  uint64_t display_id() { return display_id_; };
  uint32_t width_in_px() { return width_in_px_; };
  uint32_t height_in_px() { return height_in_px_; };
  const std::vector<zx_pixel_format_t>& pixel_formats() const {
    return pixel_formats_;
  }

  // Event signaled by DisplayManager when ownership of the display
  // changes. This event backs Scenic's GetDisplayOwnershipEvent API.
  const zx::event& ownership_event() { return ownership_event_; };

  virtual bool is_test_display() const { return false; }

 protected:
  // Protected for testing purposes.
  zx_duration_t vsync_interval_;
  zx_time_t last_vsync_time_;

 private:
  // The maximum vsync interval we would ever expect.
  static constexpr zx_duration_t kMaximumVsyncInterval = 100'000'000;  // 100 ms

  // Vsync interval of a 60 Hz screen.
  // Used as a default value before real timings arrive.
  static constexpr zx_duration_t kNsecsFor60fps = 16'666'667;

  const uint64_t display_id_;
  const uint32_t width_in_px_;
  const uint32_t height_in_px_;
  zx::event ownership_event_;
  std::vector<zx_pixel_format_t> pixel_formats_;

  bool claimed_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(Display);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_H_
