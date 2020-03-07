// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_registry.h"

namespace accessibility_test {

void MockAccessibilityFocusChainRegistry::Register(
    fxl::WeakPtr<a11y::AccessibilityFocusChainListener> listener) {
  listener_ = std::move(listener);
}

void MockAccessibilityFocusChainRegistry::SendViewRefKoid(zx_koid_t koid) {
  if (listener_) {
    listener_->OnViewFocus(koid);
  }
}

}  // namespace accessibility_test
