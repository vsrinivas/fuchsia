// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/display_controller.h"

#include "src/lib/fxl/logging.h"

namespace scenic_impl {
namespace display {

Display2::Display2(uint64_t display_id, std::vector<fuchsia::hardware::display::Mode> display_modes,
                   std::vector<zx_pixel_format_t> pixel_formats)
    : display_id_(display_id), display_modes_(std::move(display_modes)), pixel_formats_(std::move(pixel_formats)) {}

void Display2::OnVsync(zx::time timestamp, const std::vector<uint64_t>& images) {
  if (vsync_callback_) {
    vsync_callback_(timestamp, images);
  }
}

DisplayController::DisplayController(
    std::vector<Display2> displays,
    const std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr>& controller)
    : displays_(std::move(displays)), controller_(controller) {}

void DisplayController::AddDisplay(Display2 display) {
  displays_.push_back(std::move(display));
  if (on_display_added_listener_) {
    on_display_added_listener_(&displays_.back());
  }
}

bool DisplayController::RemoveDisplay(uint64_t display_id) {
  auto it = std::find_if(displays_.begin(), displays_.end(), [display_id](const Display2& display) {
    return display.display_id() == display_id;
  });
  bool found = it != displays_.end();
  FXL_DCHECK(found) << "display_id " << display_id << " not found";
  if (found) {
    displays_.erase(it);
  }
  if (on_display_removed_listener_) {
    on_display_removed_listener_(display_id);
  }
  return found;
}

}  // namespace display
}  // namespace scenic_impl
