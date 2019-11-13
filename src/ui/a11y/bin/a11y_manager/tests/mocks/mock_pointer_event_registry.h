// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_BIN_A11Y_MANAGER_TESTS_MOCKS_MOCK_POINTER_EVENT_REGISTRY_H_
#define SRC_UI_A11Y_BIN_A11Y_MANAGER_TESTS_MOCKS_MOCK_POINTER_EVENT_REGISTRY_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/lib/fxl/macros.h"

namespace accessibility_test {

// A simple mock that accepts the registration of an accessibility pointer event listener.
// Used only for testing a11y code.
class MockPointerEventRegistry : public fuchsia::ui::input::accessibility::PointerEventRegistry {
 public:
  explicit MockPointerEventRegistry(sys::testing::ComponentContextProvider* context);
  ~MockPointerEventRegistry() = default;

  fuchsia::ui::input::accessibility::PointerEventListenerPtr& listener() { return listener_; }

 private:
  // |fuchsia.ui.input.accessibility.PointerEventRegistry|
  void Register(fidl::InterfaceHandle<fuchsia::ui::input::accessibility::PointerEventListener>
                    pointer_event_listener) override;

  fidl::BindingSet<fuchsia::ui::input::accessibility::PointerEventRegistry> bindings_;

  fuchsia::ui::input::accessibility::PointerEventListenerPtr listener_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MockPointerEventRegistry);
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_BIN_A11Y_MANAGER_TESTS_MOCKS_MOCK_POINTER_EVENT_REGISTRY_H_
