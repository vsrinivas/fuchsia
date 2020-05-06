// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_listener_registry.h"

namespace a11y {

void GestureListenerRegistry::Register(
    fidl::InterfaceHandle<fuchsia::accessibility::gesture::Listener> listener,
    RegisterCallback callback) {
  listener_.Bind(std::move(listener));
  callback();
}

}  // namespace a11y
