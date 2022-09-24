// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_FOCUS_CHAIN_TESTS_MOCKS_MOCK_FOCUS_CHAIN_REQUESTER_H_
#define SRC_UI_A11Y_LIB_FOCUS_CHAIN_TESTS_MOCKS_MOCK_FOCUS_CHAIN_REQUESTER_H_

#include <fuchsia/ui/views/cpp/fidl.h>

#include <optional>

#include "src/ui/a11y/lib/focus_chain/accessibility_focus_chain_requester.h"

namespace accessibility_test {

class MockAccessibilityFocusChainRequester : public a11y::AccessibilityFocusChainRequester {
 public:
  MockAccessibilityFocusChainRequester() = default;
  ~MockAccessibilityFocusChainRequester() = default;

  // For testing only:
  void set_will_change_focus(bool result);
  const std::optional<fuchsia::ui::views::ViewRef>& ReceivedViewRef() const;
  void clear_view_ref() { view_ref_.reset(); }

  // |AccessibilityFocusChainRequester|
  void ChangeFocusToView(fuchsia::ui::views::ViewRef view_ref,
                         ChangeFocusToViewCallback callback) override;

 private:
  // Whether calls to ChangeFocusToView() will succeed.
  bool will_change_focus = true;
  // The ViewRef received in the last focus change request.
  std::optional<fuchsia::ui::views::ViewRef> view_ref_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_FOCUS_CHAIN_TESTS_MOCKS_MOCK_FOCUS_CHAIN_REQUESTER_H_
