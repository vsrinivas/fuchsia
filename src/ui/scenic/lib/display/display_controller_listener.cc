// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/display_controller_listener.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

namespace scenic_impl {
namespace display {

DisplayControllerListener::DisplayControllerListener(
    zx::channel device_channel,
    std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> controller)
    : controller_(std::move(controller)),
      controller_channel_handle_(controller_->unowned_channel()->get()),
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
    // TODO(fxbug.dev/7520): Resolve this hack when synchronous interfaces support events.
    wait_event_msg_.set_object(controller_channel_handle_);
    wait_event_msg_.set_trigger(ZX_CHANNEL_READABLE);
    wait_event_msg_.Begin(async_get_default_dispatcher());
  }
}

DisplayControllerListener::~DisplayControllerListener() {
  ClearCallbacks();

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
    fit::closure on_invalid, OnDisplaysChangedCallback on_displays_changed_cb,
    OnClientOwnershipChangeCallback on_client_ownership_change_cb) {
  FX_CHECK(!initialized_callbacks_);
  initialized_callbacks_ = true;

  on_invalid_cb_ = std::move(on_invalid);

  // TODO(fxbug.dev/7520): Resolve this hack when synchronous interfaces support events.
  auto event_dispatcher =
      static_cast<fuchsia::hardware::display::Controller::Proxy_*>(event_dispatcher_.get());
  event_dispatcher->OnDisplaysChanged = std::move(on_displays_changed_cb);
  event_dispatcher->OnClientOwnershipChange = std::move(on_client_ownership_change_cb);
}

void DisplayControllerListener::ClearCallbacks() {
  auto event_dispatcher =
      static_cast<fuchsia::hardware::display::Controller::Proxy_*>(event_dispatcher_.get());
  event_dispatcher->OnDisplaysChanged = nullptr;
  event_dispatcher->OnClientOwnershipChange = nullptr;
  event_dispatcher->OnVsync = nullptr;
  on_invalid_cb_ = nullptr;
}

void DisplayControllerListener::SetOnVsyncCallback(OnVsyncCallback on_vsync_cb) {
  // TODO(fxbug.dev/7520): Resolve this hack when synchronous interfaces support events.
  auto event_dispatcher =
      static_cast<fuchsia::hardware::display::Controller::Proxy_*>(event_dispatcher_.get());
  event_dispatcher->OnVsync = std::move(on_vsync_cb);
}

void DisplayControllerListener::OnPeerClosedAsync(async_dispatcher_t* dispatcher,
                                                  async::WaitBase* self, zx_status_t status,
                                                  const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "scenic_impl::gfx::DisplayControllerImpl: Error while waiting on "
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
    if (on_invalid_cb_) {
      // We want |on_invalid_cb_| to be cleared when we're done, so move it out.
      auto callback = std::move(on_invalid_cb_);
      callback();
      // See warning below.
    }
    // Warning!
    // Don't do anything else after callback() is invoked, since |this| could be destroyed.
    return;
  }
  FX_NOTREACHED();
}

void DisplayControllerListener::OnEventMsgAsync(async_dispatcher_t* dispatcher,
                                                async::WaitBase* self, zx_status_t status,
                                                const zx_packet_signal_t* signal) {
  // TODO(fxbug.dev/7520): Resolve this hack when synchronous interfaces support events.
  if (status != ZX_OK) {
    FX_LOGS(WARNING)
        << "scenic_impl::gfx::DisplayControllerImpl: Error while waiting on ZX_CHANNEL_READABLE: "
        << status;
    return;
  }
  if (signal->observed & ZX_CHANNEL_READABLE) {
    uint8_t byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Message msg(fidl::BytePart(byte_buffer, ZX_CHANNEL_MAX_MSG_BYTES), fidl::HandlePart());
    if (msg.Read(controller_channel_handle_, 0) != ZX_OK) {
      FX_LOGS(WARNING) << "Display controller callback read failed";
      return;
    }
    // Re-arm the wait.
    wait_event_msg_.Begin(async_get_default_dispatcher());

    static_cast<fuchsia::hardware::display::Controller::Proxy_*>(event_dispatcher_.get())
        ->Dispatch_(std::move(msg));
    return;
  }
  FX_NOTREACHED();
}

}  // namespace display
}  // namespace scenic_impl
