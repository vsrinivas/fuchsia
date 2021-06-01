// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/view/tests/mocks/mock_view_injector_factory.h"

namespace accessibility_test {

std::unique_ptr<input::Injector> MockViewInjectorFactory::BuildAndConfigureInjector(
    a11y::AccessibilityViewInterface* a11y_view, sys::ComponentContext* component_context,
    fuchsia::ui::views::ViewRef context, fuchsia::ui::views::ViewRef target) {
  if (injector_) {
    return std::move(injector_);
  }
  return nullptr;
}

}  // namespace accessibility_test
