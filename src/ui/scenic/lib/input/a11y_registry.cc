// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/a11y_registry.h"

#include <lib/syslog/cpp/macros.h>

namespace scenic_impl::input {

A11yPointerEventRegistry::A11yPointerEventRegistry(sys::ComponentContext* context,
                                                   fit::function<void()> on_register,
                                                   fit::function<void()> on_disconnect)
    : on_register_(std::move(on_register)), on_disconnect_(std::move(on_disconnect)) {
  FX_DCHECK(on_register_);
  FX_DCHECK(on_disconnect_);
  // Adding the service here is safe since the A11yPointerEventRegistry instance in InputSystem is
  // created at construction time.
  context->outgoing()->AddPublicService(accessibility_pointer_event_registry_.GetHandler(this));
}

void A11yPointerEventRegistry::Register(
    fidl::InterfaceHandle<fuchsia::ui::input::accessibility::PointerEventListener>
        pointer_event_listener,
    RegisterCallback callback) {
  if (!accessibility_pointer_event_listener()) {
    accessibility_pointer_event_listener_.Bind(std::move(pointer_event_listener));
    accessibility_pointer_event_listener_.set_error_handler(
        [this](zx_status_t) { on_disconnect_(); });
    on_register_();
    callback(/*success=*/true);
  } else {
    // An accessibility listener is already registered.
    callback(/*success=*/false);
  }
}

}  // namespace scenic_impl::input
