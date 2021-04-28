// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_VIEW_TESTS_MOCKS_MOCK_ACCESSIBILITY_VIEW_H_
#define SRC_UI_A11Y_LIB_VIEW_TESTS_MOCKS_MOCK_ACCESSIBILITY_VIEW_H_

#include "src/ui/a11y/lib/view/a11y_view.h"

namespace accessibility_test {

// Mock accessibility view. As we route more functionality through the a11y
// view, we will add to this class.
class MockAccessibilityView : public a11y::AccessibilityViewInterface {
 public:
  MockAccessibilityView() = default;
  ~MockAccessibilityView() override = default;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_VIEW_TESTS_MOCKS_MOCK_ACCESSIBILITY_VIEW_H_
