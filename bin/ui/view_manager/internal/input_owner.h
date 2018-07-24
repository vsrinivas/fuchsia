// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_INTERNAL_INPUT_OWNER_H_
#define GARNET_BIN_UI_VIEW_MANAGER_INTERNAL_INPUT_OWNER_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/fit/function.h>

namespace view_manager {

class InputConnectionImpl;
class InputDispatcherImpl;

class InputOwner {
 public:
  using OnEventDelivered = fit::function<void(bool handled)>;

  virtual ~InputOwner() {}

  // Delivers an event to a view.
  virtual void DeliverEvent(::fuchsia::ui::viewsv1token::ViewToken view_token,
                            fuchsia::ui::input::InputEvent event,
                            OnEventDelivered callback) = 0;

  // INPUT CONNECTION CALLBACKS
  virtual void OnInputConnectionDied(
      view_manager::InputConnectionImpl* connection) = 0;

  // INPUT DISPATCHER CALLBACKS
  virtual void OnInputDispatcherDied(
      view_manager::InputDispatcherImpl* dispatcher) = 0;
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_INTERNAL_INPUT_OWNER_H_
