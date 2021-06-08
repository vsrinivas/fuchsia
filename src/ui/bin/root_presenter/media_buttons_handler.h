// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_MEDIA_BUTTONS_HANDLER_H_
#define SRC_UI_BIN_ROOT_PRESENTER_MEDIA_BUTTONS_HANDLER_H_

#include <fuchsia/ui/policy/cpp/fidl.h>

#include "src/lib/ui/input/device_state.h"
#include "src/lib/ui/input/input_device_impl.h"

namespace root_presenter {

// MediaButtonsHandler tracks input devices with media buttons and notifies
// listeners of media button events originating from said devices. Listeners
// also receive an initial button state of the devices present at the time of
// registration.
class MediaButtonsHandler {
 public:
  // * `RegisterListener` adds listeners to `old_media_buttons_listeners_`
  // * `RegisterListener2` adds listeners to `media_buttons_listeners_`
  // TODO: Clean up old listener implementation after transition
  void RegisterListener(fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> listener);
  void RegisterListener2(fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> listener);
  bool OnDeviceAdded(ui_input::InputDeviceImpl* input_device);
  bool OnDeviceRemoved(uint32_t device_id);
  bool OnReport(uint32_t device_id, fuchsia::ui::input::InputReport report);

 private:
  // OnEvent is the callback invoked by the closure registered as the callback
  // for when an input device's state changes. This method then forwards the
  // event (translated into media button state) to each listener.
  void OnEvent(fuchsia::ui::input::InputReport event);

  // A registry of listeners for media button events.
  std::vector<fuchsia::ui::policy::MediaButtonsListenerPtr> media_buttons_listeners_;
  // This registry of listeners is necessary to keep track of which listeners are using the old API
  // so that the media handler can call the correct method on them. This can be removed as part of
  // fxb/68960
  std::vector<fuchsia::ui::policy::MediaButtonsListenerPtr> old_media_buttons_listeners_;

  std::map<uint32_t, std::pair<ui_input::InputDeviceImpl*, std::unique_ptr<ui_input::DeviceState>>>
      device_states_by_id_;
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_MEDIA_BUTTONS_HANDLER_H_
