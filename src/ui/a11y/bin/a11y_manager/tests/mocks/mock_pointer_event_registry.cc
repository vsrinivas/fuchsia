// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/bin/a11y_manager/tests/mocks/mock_pointer_event_registry.h"

#include <lib/sys/cpp/testing/component_context_provider.h>

namespace accessibility_test {

MockPointerEventRegistry::MockPointerEventRegistry(
    sys::testing::ComponentContextProvider* context) {
  context->service_directory_provider()->AddService(bindings_.GetHandler(this));
}

void MockPointerEventRegistry::Register(
    fidl::InterfaceHandle<fuchsia::ui::input::accessibility::PointerEventListener>
        pointer_event_listener,
    RegisterCallback callback) {
  listener_.Bind(std::move(pointer_event_listener));
  callback(true);
}

}  // namespace accessibility_test
