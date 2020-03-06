// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_FOCUS_CHAIN_TESTS_MOCKS_MOCK_FOCUS_CHAIN_REQUESTER_H_
#define SRC_UI_A11Y_LIB_FOCUS_CHAIN_TESTS_MOCKS_MOCK_FOCUS_CHAIN_REQUESTER_H_

#include <fuchsia/ui/views/cpp/fidl.h>

#include "src/ui/a11y/lib/focus_chain/accessibility_focus_chain_requester.h"

namespace accessibility_test {

class MockAccessibilityFocusChainRequester : public a11y::AccessibilityFocusChainRequester {
 public:
  MockAccessibilityFocusChainRequester() = default;
  ~MockAccessibilityFocusChainRequester() = default;

  // For testing only:
  void set_will_change_focus(bool result);
  zx_koid_t ReceivedKoid() const;

  // |AccessibilityFocusChainRequester|
  void ChangeFocusToView(zx_koid_t view_ref_koid, ChangeFocusToViewCallback callback) override;

 private:
  // Weather calls to ChangeFocusToView() will succeed or not.
  bool will_change_focus = true;
  // The ViewRef Koid received in the request.
  zx_koid_t received_koid_ = ZX_KOID_INVALID;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_FOCUS_CHAIN_TESTS_MOCKS_MOCK_FOCUS_CHAIN_REQUESTER_H_
