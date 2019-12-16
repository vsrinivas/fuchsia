// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_DISPLAY_DISPLAY_H_
#define SRC_UI_SCENIC_LIB_DISPLAY_DISPLAY_H_

#include <zircon/types.h>

#include <array>
#include <cstdint>
#include <vector>

#include "lib/zx/event.h"
#include "src/lib/fxl/macros.h"
#include "src/ui/scenic/lib/display/color_transform.h"
#include "src/ui/scenic/lib/scheduling/vsync_timing.h"
#include "zircon/pixelformat.h"

namespace scenic_impl {
namespace display {

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
  void OnVsync(zx::time timestamp);

  std::shared_ptr<const scheduling::VsyncTiming> vsync_timing() { return vsync_timing_; }

  // Claiming a display means that no other display renderer can use it.
  bool is_claimed() const { return claimed_; }
  void Claim();
  void Unclaim();

  // The display's ID in the context of the DisplayManager's DisplayController.
  uint64_t display_id() { return display_id_; };
  uint32_t width_in_px() { return width_in_px_; };
  uint32_t height_in_px() { return height_in_px_; };
  const std::vector<zx_pixel_format_t>& pixel_formats() const { return pixel_formats_; }

  // Event signaled by DisplayManager when ownership of the display
  // changes. This event backs Scenic's GetDisplayOwnershipEvent API.
  const zx::event& ownership_event() { return ownership_event_; };

 protected:
  std::shared_ptr<scheduling::VsyncTiming> vsync_timing_;

 private:
  // The maximum vsync interval we would ever expect.
  static constexpr zx::duration kMaximumVsyncInterval = zx::msec(100);

  const uint64_t display_id_;
  const uint32_t width_in_px_;
  const uint32_t height_in_px_;
  zx::event ownership_event_;
  std::vector<zx_pixel_format_t> pixel_formats_;

  bool claimed_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(Display);
};

}  // namespace display
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_DISPLAY_DISPLAY_H_
