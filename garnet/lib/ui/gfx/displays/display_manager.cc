// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/displays/display_manager.h"

#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <trace/event.h>
#include <zircon/syscalls.h>

#include "fuchsia/ui/scenic/cpp/fidl.h"

namespace scenic_impl {
namespace gfx {

DisplayManager::DisplayManager() {
  zx_status_t status = fdio_service_connect(
      "/svc/fuchsia.sysmem.Allocator",
      sysmem_allocator_.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    sysmem_allocator_.Unbind();
    FXL_LOG(ERROR) << "Unable to connect to sysmem: " << status;
  }
}

DisplayManager::~DisplayManager() {
  if (wait_.object() != ZX_HANDLE_INVALID) {
    wait_.Cancel();
  }
}

void DisplayManager::WaitForDefaultDisplay(fit::closure callback) {
  FXL_DCHECK(!default_display_);

  display_available_cb_ = std::move(callback);
  display_watcher_.WaitForDisplay(
      [this](zx::channel device, zx::channel dc_handle) {
        dc_device_ = std::move(device);
        dc_channel_ = dc_handle.get();
        display_controller_.Bind(std::move(dc_handle));

        // TODO(FIDL-183): Resolve this hack when synchronous interfaces
        // support events.
        auto dispatcher =
            static_cast<fuchsia::hardware::display::Controller::Proxy_*>(
                event_dispatcher_.get());
        dispatcher->DisplaysChanged = [this](auto added, auto removed) {
          DisplaysChanged(std::move(added), std::move(removed));
        };
        dispatcher->ClientOwnershipChange = [this](auto change) {
          ClientOwnershipChange(change);
        };
        dispatcher->Vsync = [this](uint64_t display_id, uint64_t timestamp,
                                   ::std::vector<uint64_t> images) {
          if (display_id == default_display_->display_id() && vsync_cb_) {
            vsync_cb_(timestamp, images);
          }
        };

        wait_.set_object(dc_channel_);
        wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
        wait_.Begin(async_get_default_dispatcher());
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
  static_cast<fuchsia::hardware::display::Controller::Proxy_*>(
      event_dispatcher_.get())
      ->Dispatch_(std::move(msg));
}

void DisplayManager::DisplaysChanged(
    ::std::vector<fuchsia::hardware::display::Info> added,
    ::std::vector<uint64_t> removed) {
  if (!default_display_) {
    FXL_DCHECK(added.size());

    auto& display = added[0];
    auto& mode = display.modes[0];

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
        display.id, mode.horizontal_resolution, mode.vertical_resolution,
        std::move(display.pixel_format));
    ClientOwnershipChange(owns_display_controller_);

    display_available_cb_();
    display_available_cb_ = nullptr;
  } else {
    for (uint64_t id : removed) {
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
  return fuchsia::hardware::display::invalidId;
}

void DisplayManager::ReleaseEvent(uint64_t id) {
  display_controller_->ReleaseEvent(id);
}

void DisplayManager::SetImageConfig(int32_t width, int32_t height,
                                    zx_pixel_format_t format) {
  image_config_.height = height;
  image_config_.width = width;
  image_config_.pixel_format = format;

#if defined(__x86_64__)
  // IMAGE_TYPE_X_TILED from ddk/protocol/intelgpucore.h
  image_config_.type = 1;
#elif defined(__aarch64__)
  image_config_.type = 0;
#else
  FXL_DCHECK(false) << "Display swapchain only supported on intel and ARM";
#endif

  display_controller_->SetLayerPrimaryConfig(layer_id_, image_config_);
}

uint64_t DisplayManager::ImportImage(uint64_t collection_id, uint32_t index) {
  zx_status_t status, result_status = ZX_OK;
  uint64_t id;
  status = display_controller_->ImportImage(image_config_, collection_id, index,
                                            &result_status, &id);
  if (status != ZX_OK || result_status != ZX_OK) {
    return fuchsia::hardware::display::invalidId;
  }
  return id;
}

void DisplayManager::ReleaseImage(uint64_t id) {
  display_controller_->ReleaseImage(id);
}

fuchsia::sysmem::BufferCollectionTokenSyncPtr
DisplayManager::CreateBufferCollection() {
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  zx_status_t status =
      sysmem_allocator_->AllocateSharedCollection(local_token.NewRequest());
  if (status != ZX_OK) {
    FXL_DLOG(ERROR) << "CreateBufferCollection failed " << status;
    return nullptr;
  }
  return local_token;
}

fuchsia::sysmem::BufferCollectionSyncPtr DisplayManager::GetCollectionFromToken(
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token) {
  fuchsia::sysmem::BufferCollectionSyncPtr collection;
  zx_status_t status = sysmem_allocator_->BindSharedCollection(
      std::move(token), collection.NewRequest());
  if (status != ZX_OK) {
    FXL_DLOG(ERROR) << "BindSharedCollection failed " << status;
    return nullptr;
  }
  return collection;
}

uint64_t DisplayManager::ImportBufferCollection(
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token) {
  uint64_t buffer_collection_id = next_buffer_collection_id_++;
  zx_status_t status;
  if (display_controller_->ImportBufferCollection(
          buffer_collection_id, std::move(token), &status) != ZX_OK ||
      status != ZX_OK) {
    FXL_DLOG(ERROR) << "ImportBufferCollection failed";
    return 0;
  }

  if (display_controller_->SetBufferCollectionConstraints(
          buffer_collection_id, image_config_, &status) != ZX_OK ||
      status != ZX_OK) {
    FXL_DLOG(ERROR) << "SetBufferCollectionConstraints failed.";
    display_controller_->ReleaseBufferCollection(buffer_collection_id);
    return 0;
  }

  return buffer_collection_id;
}

void DisplayManager::ReleaseBufferCollection(uint64_t id) {
  display_controller_->ReleaseBufferCollection(id);
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
}  // namespace scenic_impl
