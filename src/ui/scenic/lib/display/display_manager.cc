// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/display_manager.h"

#include <fuchsia/ui/scenic/cpp/fidl.h>

#include "lib/fit/function.h"
#include "src/lib/fxl/logging.h"

namespace scenic_impl {
namespace gfx {

void DisplayManager::WaitForDefaultDisplayController(fit::closure display_available_cb) {
  FXL_DCHECK(!default_display_);

  display_available_cb_ = std::move(display_available_cb);

  dc_watcher_.WaitForDisplayController([this](zx::channel dc_device, zx::channel dc_channel) {
    BindDefaultDisplayController(std::move(dc_device), std::move(dc_channel));
  });
}

void DisplayManager::BindDefaultDisplayController(zx::channel dc_device, zx::channel dc_channel) {
  // TODO(36549): Don't need to pass |dc_channel_handle| as a separate arg when
  // SynchronousInterfacePtr gets a channel() getter.
  zx_handle_t dc_channel_handle = dc_channel.get();
  default_display_controller_ = std::make_shared<fuchsia::hardware::display::ControllerSyncPtr>();
  default_display_controller_->Bind(std::move(dc_channel));
  default_display_controller_listener_ = std::make_shared<display::DisplayControllerListener>(
      std::move(dc_device), default_display_controller_, dc_channel_handle);
  default_display_controller_listener_->InitializeCallbacks(
      /*on_invalid_cb=*/nullptr, fit::bind_member(this, &DisplayManager::OnDisplaysChanged),
      fit::bind_member(this, &DisplayManager::OnClientOwnershipChange));
}

void DisplayManager::OnDisplaysChanged(std::vector<fuchsia::hardware::display::Info> added,
                                       std::vector<uint64_t> removed) {
  if (!default_display_) {
    FXL_DCHECK(added.size());

    auto& display = added[0];
    auto& mode = display.modes[0];

    default_display_ =
        std::make_unique<Display>(display.id, mode.horizontal_resolution, mode.vertical_resolution,
                                  std::move(display.pixel_format));
    OnClientOwnershipChange(owns_display_controller_);

    display_available_cb_();
    display_available_cb_ = nullptr;
  } else {
    for (uint64_t id : removed) {
      if (default_display_->display_id() == id) {
        // TODO(SCN-244): handle this more robustly.
        FXL_CHECK(false) << "Display disconnected";
        return;
      }
    }
  }
}

void DisplayManager::OnClientOwnershipChange(bool has_ownership) {
  owns_display_controller_ = has_ownership;
  if (default_display_) {
    if (has_ownership) {
      default_display_->ownership_event().signal(fuchsia::ui::scenic::displayNotOwnedSignal,
                                                 fuchsia::ui::scenic::displayOwnedSignal);
    } else {
      default_display_->ownership_event().signal(fuchsia::ui::scenic::displayOwnedSignal,
                                                 fuchsia::ui::scenic::displayNotOwnedSignal);
    }
  }
}

}  // namespace gfx
}  // namespace scenic_impl
