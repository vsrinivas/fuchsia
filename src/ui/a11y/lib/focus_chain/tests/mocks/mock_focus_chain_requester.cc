// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_requester.h"

namespace accessibility_test {

void MockAccessibilityFocusChainRequester::set_will_change_focus(bool result) {
  will_change_focus = result;
}

void MockAccessibilityFocusChainRequester::ChangeFocusToView(zx_koid_t view_ref_koid,
                                                             ChangeFocusToViewCallback callback) {
  callback(will_change_focus);
}

}  // namespace accessibility_test
