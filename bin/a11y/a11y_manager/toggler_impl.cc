// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "garnet/bin/a11y/a11y_manager/toggler_impl.h"

namespace a11y_manager {

TogglerImpl::TogglerImpl() : toggler_binding_(this) {}

void TogglerImpl::AddTogglerBinding(
    fidl::InterfaceRequest<fuchsia::accessibility::Toggler> request) {
  toggler_binding_.Bind(std::move(request));
}

void TogglerImpl::AddToggleBroadcasterBinding(
    fidl::InterfaceRequest<fuchsia::accessibility::ToggleBroadcaster> request) {
  broadcaster_bindings_.AddBinding(this, std::move(request));
  broadcaster_bindings_.bindings().back().get()->events().OnAccessibilityToggle(
      is_enabled_);
}

void TogglerImpl::ToggleAccessibilitySupport(bool enabled) {
  is_enabled_ = enabled;
  for (auto it = broadcaster_bindings_.bindings().begin();
       it != broadcaster_bindings_.bindings().end(); ++it) {
    it->get()->events().OnAccessibilityToggle(is_enabled_);
  }
}

}  // namespace a11y_manager
