// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/display_manager.h"

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "lib/fit/function.h"

namespace scenic_impl {
namespace display {

DisplayManager::DisplayManager(fit::closure display_available_cb)
    : DisplayManager(std::nullopt, std::nullopt, std::move(display_available_cb)) {}

DisplayManager::DisplayManager(std::optional<uint64_t> i_can_haz_display_id,
                               std::optional<uint64_t> i_can_haz_display_mode,
                               fit::closure display_available_cb)
    : i_can_haz_display_id_(i_can_haz_display_id),
      i_can_haz_display_mode_(i_can_haz_display_mode),
      display_available_cb_(std::move(display_available_cb)) {}

void DisplayManager::BindDefaultDisplayController(
    fidl::InterfaceHandle<fuchsia::hardware::display::Controller> controller) {
  FX_DCHECK(!default_display_controller_);
  FX_DCHECK(controller);
  default_display_controller_ = std::make_shared<fuchsia::hardware::display::ControllerSyncPtr>();
  default_display_controller_->Bind(std::move(controller));
  default_display_controller_listener_ =
      std::make_shared<display::DisplayControllerListener>(default_display_controller_);
  default_display_controller_listener_->InitializeCallbacks(
      /*on_invalid_cb=*/nullptr, fit::bind_member<&DisplayManager::OnDisplaysChanged>(this),
      fit::bind_member<&DisplayManager::OnClientOwnershipChange>(this));

  // Set up callback to handle Vsync notifications, and ask controller to send these notifications.
  default_display_controller_listener_->SetOnVsyncCallback(
      fit::bind_member<&DisplayManager::OnVsync>(this));
  zx_status_t vsync_status = (*default_display_controller_)->EnableVsync(true);
  if (vsync_status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to enable vsync, status: " << vsync_status;
  }
}

void DisplayManager::OnDisplaysChanged(std::vector<fuchsia::hardware::display::Info> added,
                                       std::vector<uint64_t> removed) {
  for (auto& display : added) {
    // Ignore display if |i_can_haz_display_id| is set and it doesn't match ID.
    if (i_can_haz_display_id_.has_value() && display.id != *i_can_haz_display_id_) {
      FX_LOGS(INFO) << "Ignoring display with id=" << display.id
                    << " ... waiting for display with id=" << *i_can_haz_display_id_;
      continue;
    }

    if (!default_display_) {
      size_t mode_idx = 0;

      // Set display mode if requested.
      if (i_can_haz_display_mode_.has_value()) {
        if (*i_can_haz_display_mode_ < display.modes.size()) {
          mode_idx = *i_can_haz_display_mode_;
          (*default_display_controller_)->SetDisplayMode(display.id, display.modes[mode_idx]);
          (*default_display_controller_)->ApplyConfig();
        } else {
          FX_LOGS(ERROR) << "Requested display mode=" << *i_can_haz_display_mode_
                         << " doesn't exist for display with id=" << display.id;
        }
      }

      auto& mode = display.modes[mode_idx];
      default_display_ = std::make_unique<Display>(
          display.id, mode.horizontal_resolution, mode.vertical_resolution,
          display.horizontal_size_mm, display.vertical_size_mm, std::move(display.pixel_format));
      OnClientOwnershipChange(owns_display_controller_);

      if (display_available_cb_) {
        display_available_cb_();
        display_available_cb_ = nullptr;
      }
    }
  }

  for (uint64_t id : removed) {
    if (default_display_ && default_display_->display_id() == id) {
      // TODO(fxbug.dev/23490): handle this more robustly.
      FX_CHECK(false) << "Display disconnected";
      return;
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

void DisplayManager::SetVsyncCallback(VsyncCallback callback) {
  FX_DCHECK(!(static_cast<bool>(callback) && static_cast<bool>(vsync_callback_)))
      << "cannot stomp vsync callback.";

  vsync_callback_ = std::move(callback);
}

void DisplayManager::OnVsync(uint64_t display_id, uint64_t timestamp,
                             fuchsia::hardware::display::ConfigStamp applied_config_stamp,
                             uint64_t cookie) {
  if (cookie) {
    (*default_display_controller_)->AcknowledgeVsync(cookie);
  }

  if (vsync_callback_) {
    vsync_callback_(display_id, zx::time(timestamp), applied_config_stamp);
  }

  if (!default_display_) {
    return;
  }
  if (default_display_->display_id() != display_id) {
    return;
  }
  default_display_->OnVsync(zx::time(timestamp), applied_config_stamp);
}

}  // namespace display
}  // namespace scenic_impl
