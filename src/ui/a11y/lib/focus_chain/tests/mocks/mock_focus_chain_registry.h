// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_FOCUS_CHAIN_TESTS_MOCKS_MOCK_FOCUS_CHAIN_REGISTRY_H_
#define SRC_UI_A11Y_LIB_FOCUS_CHAIN_TESTS_MOCKS_MOCK_FOCUS_CHAIN_REGISTRY_H_

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/a11y/lib/focus_chain/accessibility_focus_chain_listener.h"

namespace accessibility_test {

class MockAccessibilityFocusChainRegistry : public a11y::AccessibilityFocusChainRegistry {
 public:
  MockAccessibilityFocusChainRegistry() = default;
  ~MockAccessibilityFocusChainRegistry() = default;

  // |AccessibilityFocusChainRegistry|
  void Register(fxl::WeakPtr<a11y::AccessibilityFocusChainListener> listener) override;

  // Sends a new koid to the registered |listener_|.
  void SendViewRefKoid(zx_koid_t koid);

 private:
  fxl::WeakPtr<a11y::AccessibilityFocusChainListener> listener_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_FOCUS_CHAIN_TESTS_MOCKS_MOCK_FOCUS_CHAIN_REGISTRY_H_
