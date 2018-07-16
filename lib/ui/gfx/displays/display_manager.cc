// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/displays/display_manager.h"

#include <lib/async/default.h>
#include <zircon/device/display-controller.h>
#include <zircon/pixelformat.h>
#include <zircon/syscalls.h>
#include "fuchsia/ui/scenic/cpp/fidl.h"

#include "garnet/lib/ui/gfx/displays/display_watcher.h"
#include "garnet/lib/ui/gfx/resources/renderers/renderer.h"

namespace scenic {
namespace gfx {

DisplayManager::DisplayManager() = default;

DisplayManager::~DisplayManager() {
  if (wait_.object() != ZX_HANDLE_INVALID) {
    wait_.Cancel();
  }
}

void DisplayManager::WaitForDefaultDisplay(fit::closure callback) {
  FXL_DCHECK(!default_display_);

  display_available_cb_ = std::move(callback);
  display_watcher_.WaitForDisplay(
      [this](fxl::UniqueFD fd, zx::channel dc_handle) {
  // See declare_args() in lib/ui/gfx/BUILD.gn
#if SCENIC_VULKAN_SWAPCHAIN == 2
        // This is just for testing, so notify that there's a fake display
        // that's 800x608. Without a display the scene manager won't try to draw
        // anything.
        default_display_ = std::make_unique<Display>(0, 800, 608);
        display_available_cb_();
        display_available_cb_ = nullptr;
#else
        dc_fd_ = std::move(fd);
        dc_channel_ = dc_handle.get();
        display_controller_.Bind(std::move(dc_handle));

        // TODO(FIDL-183): Resolve this hack when synchronous interfaces
        // support events.
        auto dispatcher = static_cast<fuchsia::display::Controller::Proxy_*>(
            event_dispatcher_.get());
        dispatcher->DisplaysChanged = [this](auto added, auto removed) {
          DisplaysChanged(std::move(added), std::move(removed));
        };
        dispatcher->ClientOwnershipChange = [this](auto change) {
          ClientOwnershipChange(change);
        };
        dispatcher->Vsync = [this](uint64_t display_id, uint64_t timestamp,
                                   ::fidl::VectorPtr<uint64_t> images) {
          if (display_id == default_display_->display_id() && vsync_cb_) {
            vsync_cb_(timestamp, images.get());
          }
        };

        wait_.set_object(dc_channel_);
        wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
        wait_.Begin(async_get_default_dispatcher());
#endif
      });
}

void DisplayManager::OnAsync(async_dispatcher_t* dispatcher,
                             async::WaitBase* self, zx_status_t status,
                             const zx_packet_signal_t* signal) {
  if (status & ZX_CHANNEL_PEER_CLOSED) {
    // TODO(SCN-244): handle this more robustly.
    FXL_DCHECK(false) << "Display channel lost";
    return;
  }

  // Read FIDL message.
  uint8_t byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
  fidl::Message msg(fidl::BytePart(byte_buffer, ZX_CHANNEL_MAX_MSG_BYTES),
                    fidl::HandlePart());
  if (msg.Read(dc_channel_, 0) != ZX_OK || !msg.has_header()) {
    FXL_LOG(WARNING) << "Display callback read failed";
    return;
  }
  // Re-arm the wait.
  wait_.Begin(async_get_default_dispatcher());

  // TODO(FIDL-183): Resolve this hack when synchronous interfaces
  // support events.
  static_cast<fuchsia::display::Controller::Proxy_*>(event_dispatcher_.get())
      ->Dispatch_(std::move(msg));
}

void DisplayManager::DisplaysChanged(
    ::fidl::VectorPtr<fuchsia::display::Info> added,
    ::fidl::VectorPtr<uint64_t> removed) {
  if (!default_display_) {
    FXL_DCHECK(added.get().size());

    auto& display = added.get()[0];
    auto& mode = display.modes.get()[0];

    zx_status_t status;
    zx_status_t transport_status =
        display_controller_->CreateLayer(&status, &layer_id_);
    if (transport_status != ZX_OK || status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create layer";
      return;
    }

    std::vector<uint64_t> layers;
    layers.push_back(layer_id_);
    ::fidl::VectorPtr<uint64_t> fidl_layers(std::move(layers));
    status = display_controller_->SetDisplayLayers(display.id,
                                                   std::move(fidl_layers));
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to configure display layers";
      return;
    }

    default_display_ = std::make_unique<Display>(
        display.id, mode.horizontal_resolution, mode.vertical_resolution);
    ClientOwnershipChange(owns_display_controller_);

    // See declare_args() in lib/ui/gfx/BUILD.gn.
#if SCENIC_VULKAN_SWAPCHAIN != 0
    // Vulkan swapchains don't necessarily support the concurrent
    // connection to the display controller.
    wait_.Cancel();
    display_controller_.Bind(zx::channel());
    dc_fd_.reset();
#endif

    display_available_cb_();
    display_available_cb_ = nullptr;
  } else {
    for (uint64_t id : removed.get()) {
      if (default_display_->display_id() == id) {
        // TODO(SCN-244): handle this more robustly.
        FXL_DCHECK(false) << "Display disconnected";
        wait_.Cancel();
        return;
      }
    }
  }
}

void DisplayManager::ClientOwnershipChange(bool has_ownership) {
  owns_display_controller_ = has_ownership;
  if (default_display_) {
    if (has_ownership) {
      default_display_->ownership_event().signal(
          fuchsia::ui::scenic::displayNotOwnedSignal,
          fuchsia::ui::scenic::displayOwnedSignal);
    } else {
      default_display_->ownership_event().signal(
          fuchsia::ui::scenic::displayOwnedSignal,
          fuchsia::ui::scenic::displayNotOwnedSignal);
    }
  }
}

uint64_t DisplayManager::ImportEvent(const zx::event& event) {
  zx::event dup;
  uint64_t event_id = next_event_id_++;
  if (event.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup) == ZX_OK &&
      display_controller_->ImportEvent(std::move(dup), event_id) == ZX_OK) {
    return event_id;
  }
  return fuchsia::display::invalidId;
}

void DisplayManager::ReleaseEvent(uint64_t id) {
  display_controller_->ReleaseEvent(id);
}

uint32_t DisplayManager::FetchLinearStride(uint32_t width,
                                           zx_pixel_format_t format) {
  uint32_t stride = 0;
  display_controller_->ComputeLinearImageStride(width, format, &stride);
  return stride;
}

void DisplayManager::SetImageConfig(int32_t width, int32_t height,
                                    zx_pixel_format_t format) {
  image_config_.height = height;
  image_config_.width = width;
  image_config_.pixel_format = format;

#if defined(__x86_64__)
  // IMAGE_TYPE_X_TILED from ddk/protocol/intel-gpu-core.h
  image_config_.type = 1;
#else
  FXL_DCHECK(false) << "Display swapchain only supported on intel";
#endif

  display_controller_->SetLayerPrimaryConfig(layer_id_, image_config_);
}

uint64_t DisplayManager::ImportImage(const zx::vmo& vmo) {
  zx::vmo vmo_dup;
  uint64_t id;
  zx_status_t status;
  if (vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_dup) == ZX_OK &&
      display_controller_->ImportVmoImage(image_config_, std::move(vmo_dup), 0,
                                          &status, &id) == ZX_OK &&
      status == ZX_OK) {
    return id;
  }
  return fuchsia::display::invalidId;
}

void DisplayManager::ReleaseImage(uint64_t id) {
  display_controller_->ReleaseImage(id);
}

void DisplayManager::Flip(Display* display, uint64_t buffer,
                          uint64_t render_finished_event_id,
                          uint64_t signal_event_id) {
  zx_status_t status = display_controller_->SetLayerImage(
      layer_id_, buffer, render_finished_event_id, signal_event_id);
  // TODO(SCN-244): handle this more robustly.
  FXL_DCHECK(status == ZX_OK) << "DisplayManager::Flip failed";

  status = display_controller_->ApplyConfig();
  // TODO(SCN-244): handle this more robustly.
  FXL_DCHECK(status == ZX_OK) << "DisplayManager::Flip failed";
}

bool DisplayManager::EnableVsync(VsyncCallback vsync_cb) {
  vsync_cb_ = std::move(vsync_cb);
  return display_controller_->EnableVsync(true) == ZX_OK;
}

}  // namespace gfx
}  // namespace scenic
