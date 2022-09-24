// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_requester.h"

#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {

void MockAccessibilityFocusChainRequester::set_will_change_focus(bool result) {
  will_change_focus = result;
}

const std::optional<fuchsia::ui::views::ViewRef>&
MockAccessibilityFocusChainRequester::ReceivedViewRef() const {
  return view_ref_;
}

void MockAccessibilityFocusChainRequester::ChangeFocusToView(fuchsia::ui::views::ViewRef view_ref,
                                                             ChangeFocusToViewCallback callback) {
  view_ref_ = std::move(view_ref);
  callback(will_change_focus);
}

}  // namespace accessibility_test
