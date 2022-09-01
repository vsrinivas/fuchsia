// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_DISPLAY_DISPLAY_H_
#define SRC_UI_SCENIC_LIB_DISPLAY_DISPLAY_H_

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/zx/event.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

#include <array>
#include <cstdint>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/ui/scenic/lib/display/color_transform.h"
#include "src/ui/scenic/lib/scheduling/vsync_timing.h"

#include <glm/glm.hpp>

namespace scenic_impl {
namespace display {

// Display is a placeholder that provides make-believe values for screen
// resolution, vsync interval, last vsync time, etc.
class Display {
 public:
  Display(uint64_t id, uint32_t width_in_px, uint32_t height_in_px, uint32_t width_in_mm,
          uint32_t height_in_mm, std::vector<zx_pixel_format_t> pixel_formats);
  Display(uint64_t id, uint32_t width_in_px, uint32_t height_in_px);
  virtual ~Display() = default;

  using VsyncCallback = fit::function<void(
      zx::time timestamp, fuchsia::hardware::display::ConfigStamp applied_config_stamp)>;
  void SetVsyncCallback(VsyncCallback callback) { vsync_callback_ = std::move(callback); }

  std::shared_ptr<const scheduling::VsyncTiming> vsync_timing() { return vsync_timing_; }

  // Claiming a display means that no other display renderer can use it.
  bool is_claimed() const { return claimed_; }
  void Claim();
  void Unclaim();

  // Sets the device_pixel ratio that should be used for this specific Display.
  void set_device_pixel_ratio(const glm::vec2& device_pixel_ratio) {
    device_pixel_ratio_.store(device_pixel_ratio);
  }

  // The display's ID in the context of the DisplayManager's DisplayController.
  uint64_t display_id() const { return display_id_; }
  uint32_t width_in_px() const { return width_in_px_; }
  uint32_t height_in_px() const { return height_in_px_; }
  uint32_t width_in_mm() const { return width_in_mm_; }
  uint32_t height_in_mm() const { return height_in_mm_; }

  glm::vec2 device_pixel_ratio() const { return glm::vec2(1.f, 1.f); }
  
  // TODO(fxb/99312): Remove real_device_pixel_ratio() when we complete the scale
  // work in tree and all clients.
  glm::vec2 real_device_pixel_ratio() const { return device_pixel_ratio_.load(); }

  // TODO(fxbug.dev/71410): Remove all references to zx_pixel_format_t.
  const std::vector<zx_pixel_format_t>& pixel_formats() const { return pixel_formats_; }

  // Event signaled by DisplayManager when ownership of the display
  // changes. This event backs Scenic's GetDisplayOwnershipEvent API.
  const zx::event& ownership_event() const { return ownership_event_; }

  // Called by DisplayManager, other users of Display should probably not call this.  Except tests.
  void OnVsync(zx::time timestamp, fuchsia::hardware::display::ConfigStamp applied_config_stamp);

 protected:
  std::shared_ptr<scheduling::VsyncTiming> vsync_timing_;

 private:
  VsyncCallback vsync_callback_;

  // The maximum vsync interval we would ever expect.
  static constexpr zx::duration kMaximumVsyncInterval = zx::msec(100);

  const uint64_t display_id_;
  const uint32_t width_in_px_;
  const uint32_t height_in_px_;
  const uint32_t width_in_mm_;
  const uint32_t height_in_mm_;
  // |device_pixel_ratio_| may be written from FlatlandDisplay thread and read by SingletonDisplay
  // service running on the main thread.
  std::atomic<glm::vec2> device_pixel_ratio_;
  zx::event ownership_event_;
  std::vector<zx_pixel_format_t> pixel_formats_;

  bool claimed_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(Display);
};

}  // namespace display
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_DISPLAY_DISPLAY_H_
