// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_DISPLAY_DISPLAY_CONTROLLER_LISTENER_H_
#define SRC_UI_SCENIC_LIB_DISPLAY_DISPLAY_CONTROLLER_LISTENER_H_

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>

#include "lib/fidl/cpp/synchronous_interface_ptr.h"

namespace scenic_impl {
namespace display {

// DisplayControllerListener wraps a |fuchsia::hardware::display::Controller| interface, allowing
// registering for event callbacks.
class DisplayControllerListener {
 public:
  using OnDisplaysChangedCallback = std::function<void(
      std::vector<fuchsia::hardware::display::Info> added, std::vector<uint64_t> removed)>;
  using OnClientOwnershipChangeCallback = std::function<void(bool has_ownership)>;
  using OnVsyncCallback = std::function<void(uint64_t display_id, uint64_t timestamp,
                                             std::vector<uint64_t> images, uint64_t cookie)>;

  // Binds to a Display fuchsia::hardware::display::Controller with channels |device| and
  // with display controller |controller|. |controller_handle| is the raw handle wrapped by
  // |controller|; unfortunately it must be passed separately since there's no  way to get it from
  // |controller|.
  //
  // If |device| or |controller_handle| is invalid, or |controller| is not bound, this instance is
  // invalid.
  DisplayControllerListener(
      zx::channel device_channel,
      std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> controller);
  ~DisplayControllerListener();

  // If any of the channels gets disconnected, |on_invalid| is invoked and this object becomes
  // invalid.
  void InitializeCallbacks(fit::closure on_invalid,
                           OnDisplaysChangedCallback on_displays_changed_cb,
                           OnClientOwnershipChangeCallback on_client_ownership_change_cb);

  // Removes all callbacks. Once this is done, there is no way to re-initialize the callbacks.
  void ClearCallbacks();

  void SetOnVsyncCallback(OnVsyncCallback vsync_callback);

  // Whether the connection to the display controller driver is still valid.
  bool valid() { return valid_; }

 private:
  void OnPeerClosedAsync(async_dispatcher_t* dispatcher, async::WaitBase* self, zx_status_t status,
                         const zx_packet_signal_t* signal);
  void OnEventMsgAsync(async_dispatcher_t* dispatcher, async::WaitBase* self, zx_status_t status,
                       const zx_packet_signal_t* signal);

  // The display controller driver binding.
  std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> controller_;

  // True if we're connected to |controller_|.
  bool valid_ = false;

  // |controller_| owns |controller_channel_handle_|, but save its handle here for use.
  zx_handle_t controller_channel_handle_ = 0;

  // |device_channel_| needs to be kept alive to stay connected to |controller_|.
  zx::channel device_channel_;

  // Callback to invoke if we disconnect from |controller_|.
  fit::closure on_invalid_cb_ = nullptr;

  // True if InitializeCallbacks was called; it can only be called once.
  bool initialized_callbacks_ = false;

  // Waits for a ZX_CHANNEL_READABLE signal.
  async::WaitMethod<DisplayControllerListener, &DisplayControllerListener::OnEventMsgAsync>
      wait_event_msg_{this};
  // Wait for a ZX_PEER_CLOSED signal.
  async::WaitMethod<DisplayControllerListener, &DisplayControllerListener::OnPeerClosedAsync>
      wait_device_closed_{this};
  async::WaitMethod<DisplayControllerListener, &DisplayControllerListener::OnPeerClosedAsync>
      wait_controller_closed_{this};

  // Used for dispatching events that we receive over the controller channel.
  // TODO(fxbug.dev/7520): Resolve this hack when synchronous interfaces support events.
  fidl::InterfacePtr<fuchsia::hardware::display::Controller> event_dispatcher_;
};

}  // namespace display
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_DISPLAY_DISPLAY_CONTROLLER_LISTENER_H_
