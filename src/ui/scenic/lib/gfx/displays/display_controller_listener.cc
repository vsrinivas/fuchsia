// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/displays/display_controller_listener.h"

#include <lib/async/default.h>
#include <zircon/types.h>

#include "src/lib/fxl/logging.h"

namespace scenic_impl {
namespace gfx {

DisplayControllerListener::DisplayControllerListener(
    zx::channel device_channel,
    std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> controller,
    zx_handle_t controller_channel)
    : controller_(std::move(controller)),
      controller_channel_handle_(controller_channel),
      device_channel_(std::move(device_channel)) {
  valid_ = device_channel_.is_valid() && controller_channel_handle_ != 0 && controller_->is_bound();
  if (valid_) {
    // Listen for when the device channel closes.
    wait_device_closed_.set_object(device_channel_.get());
    wait_device_closed_.set_trigger(ZX_CHANNEL_PEER_CLOSED);
    wait_device_closed_.Begin(async_get_default_dispatcher());

    // Listen for when the controller channel closes.
    wait_controller_closed_.set_object(controller_channel_handle_);
    wait_controller_closed_.set_trigger(ZX_CHANNEL_PEER_CLOSED);
    wait_controller_closed_.Begin(async_get_default_dispatcher());

    // Listen for events
    // TODO(FIDL-183): Resolve this hack when synchronous interfaces support events.
    wait_event_msg_.set_object(controller_channel_handle_);
    wait_event_msg_.set_trigger(ZX_CHANNEL_READABLE);
    wait_event_msg_.Begin(async_get_default_dispatcher());
  }
}

DisplayControllerListener::~DisplayControllerListener() {
  if (wait_event_msg_.object() != ZX_HANDLE_INVALID) {
    wait_event_msg_.Cancel();
  }
  if (wait_device_closed_.object() != ZX_HANDLE_INVALID) {
    wait_device_closed_.Cancel();
  }
  if (wait_controller_closed_.object() != ZX_HANDLE_INVALID) {
    wait_controller_closed_.Cancel();
  }
}

void DisplayControllerListener::InitializeCallbacks(
    fit::closure on_invalid, DisplaysChangedCallback displays_changed_cb,
    ClientOwnershipChangeCallback client_ownership_change_cb) {
  FXL_CHECK(!initialized_callbacks_);
  initialized_callbacks_ = true;

  on_invalid_cb_ = std::move(on_invalid);

  // TODO(FIDL-183): Resolve this hack when synchronous interfaces support events.
  auto event_dispatcher =
      static_cast<fuchsia::hardware::display::Controller::Proxy_*>(event_dispatcher_.get());
  event_dispatcher->DisplaysChanged = std::move(displays_changed_cb);
  event_dispatcher->ClientOwnershipChange = std::move(client_ownership_change_cb);
}

void DisplayControllerListener::SetVsyncCallback(VsyncCallback vsync_cb) {
  // TODO(FIDL-183): Resolve this hack when synchronous interfaces support events.
  auto event_dispatcher =
      static_cast<fuchsia::hardware::display::Controller::Proxy_*>(event_dispatcher_.get());
  event_dispatcher->Vsync = std::move(vsync_cb);
}

void DisplayControllerListener::OnPeerClosedAsync(async_dispatcher_t* dispatcher,
                                                  async::WaitBase* self, zx_status_t status,
                                                  const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "scenic_impl::gfx::DisplayControllerImpl: Error while waiting on "
                        "ZX_CHANNEL_PEER_CLOSED: "
                     << status;
    return;
  }

  if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
    valid_ = false;

    // We don't want to get another callback, and we don't know which channel closed, so just cancel
    // both waits.
    wait_device_closed_.Cancel();
    wait_controller_closed_.Cancel();
    auto callback = std::move(on_invalid_cb_);
    callback();
    // Don't do anything else here, since this could be destroyed.
    return;
  }
  FXL_NOTREACHED();
}

void DisplayControllerListener::OnEventMsgAsync(async_dispatcher_t* dispatcher,
                                                async::WaitBase* self, zx_status_t status,
                                                const zx_packet_signal_t* signal) {
  // TODO(FIDL-183): Resolve this hack when synchronous interfaces support events.
  if (status != ZX_OK) {
    FXL_LOG(WARNING)
        << "scenic_impl::gfx::DisplayControllerImpl: Error while waiting on ZX_CHANNEL_READABLE: "
        << status;
    return;
  }
  if (signal->observed & ZX_CHANNEL_READABLE) {
    uint8_t byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Message msg(fidl::BytePart(byte_buffer, ZX_CHANNEL_MAX_MSG_BYTES), fidl::HandlePart());
    if (msg.Read(controller_channel_handle_, 0) != ZX_OK || !msg.has_header()) {
      FXL_LOG(WARNING) << "Display controller callback read failed";
      return;
    }
    // Re-arm the wait.
    wait_event_msg_.Begin(async_get_default_dispatcher());

    static_cast<fuchsia::hardware::display::Controller::Proxy_*>(event_dispatcher_.get())
        ->Dispatch_(std::move(msg));
    return;
  }
  FXL_NOTREACHED();
}

}  // namespace gfx
}  // namespace scenic_impl
