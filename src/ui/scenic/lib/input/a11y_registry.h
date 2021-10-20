// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_A11Y_REGISTRY_H_
#define SRC_UI_SCENIC_LIB_INPUT_A11Y_REGISTRY_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

namespace scenic_impl::input {

// Implementation of PointerEventRegistry API.
class A11yPointerEventRegistry : public fuchsia::ui::input::accessibility::PointerEventRegistry {
 public:
  A11yPointerEventRegistry(sys::ComponentContext* context, fit::function<void()> on_register,
                           fit::function<void()> on_disconnect);

  // |fuchsia.ui.input.accessibility.PointerEventRegistry|
  void Register(fidl::InterfaceHandle<fuchsia::ui::input::accessibility::PointerEventListener>
                    pointer_event_listener,
                RegisterCallback callback) override;

  fuchsia::ui::input::accessibility::PointerEventListenerPtr&
  accessibility_pointer_event_listener() {
    return accessibility_pointer_event_listener_;
  }

 private:
  fidl::BindingSet<fuchsia::ui::input::accessibility::PointerEventRegistry>
      accessibility_pointer_event_registry_;
  // We honor the first accessibility listener to register. A call to Register()
  // above will fail if there is already a registered listener.
  fuchsia::ui::input::accessibility::PointerEventListenerPtr accessibility_pointer_event_listener_;

  // Function called when a new listener successfully registers.
  fit::function<void()> on_register_;

  // Function called when an active listener disconnects.
  fit::function<void()> on_disconnect_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_A11Y_REGISTRY_H_
