// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_VIEW_MANAGER_INTERNAL_INPUT_OWNER_H_
#define APPS_MOZART_SRC_VIEW_MANAGER_INTERNAL_INPUT_OWNER_H_

#include "lib/ui/input/fidl/input_connection.fidl.h"
#include "lib/ui/views/fidl/view_token.fidl.h"

namespace view_manager {

class InputConnectionImpl;
class InputDispatcherImpl;

class InputOwner {
 public:
  using OnEventDelivered = std::function<void(bool handled)>;

  virtual ~InputOwner() {}

  // Delivers an event to a view.
  virtual void DeliverEvent(const mozart::ViewToken* view_token,
                            mozart::InputEventPtr event,
                            OnEventDelivered callback) = 0;

  // INPUT CONNECTION CALLBACKS
  virtual void OnInputConnectionDied(
      view_manager::InputConnectionImpl* connection) = 0;

  // INPUT DISPATCHER CALLBACKS
  virtual void OnInputDispatcherDied(
      view_manager::InputDispatcherImpl* dispatcher) = 0;
};

}  // namespace view_manager

#endif  // APPS_MOZART_SRC_VIEW_MANAGER_INTERNAL_INPUT_OWNER_H_
