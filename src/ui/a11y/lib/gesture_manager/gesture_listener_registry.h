// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_LISTENER_REGISTRY_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_LISTENER_REGISTRY_H_

#include <fuchsia/accessibility/gesture/cpp/fidl.h>
#include <fuchsia/ui/input/accessibility/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"

namespace a11y {

// The GestureListenerRegistry class implements the protocol
// |fuchsia.accessibility.gesture.ListenerRegistry|, and offers a way to access the registered
// listener.
class GestureListenerRegistry : public fuchsia::accessibility::gesture::ListenerRegistry {
 public:
  GestureListenerRegistry() = default;
  virtual ~GestureListenerRegistry() = default;

  // |fuchsia.accessibility.gesture.ListenerRegistry|
  void Register(fidl::InterfaceHandle<fuchsia::accessibility::gesture::Listener> listener,
                RegisterCallback callback) override;

  // Returns the listener registered with this class.
  fuchsia::accessibility::gesture::ListenerPtr& listener() { return listener_; }

 private:
  fuchsia::accessibility::gesture::ListenerPtr listener_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_LISTENER_REGISTRY_H_
