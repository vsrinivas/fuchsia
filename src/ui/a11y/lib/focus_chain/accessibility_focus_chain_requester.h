// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_FOCUS_CHAIN_ACCESSIBILITY_FOCUS_CHAIN_REQUESTER_H_
#define SRC_UI_A11Y_LIB_FOCUS_CHAIN_ACCESSIBILITY_FOCUS_CHAIN_REQUESTER_H_

#include <fuchsia/ui/views/cpp/fidl.h>

namespace a11y {

// An interface for a11y request Focus Chain Updates.
class AccessibilityFocusChainRequester {
 public:
  // Callback that indicates when the Focus Chain request is done processing. Returns true on
  // success, false otherwise.
  using ChangeFocusToViewCallback = fit::function<void(bool)>;
  AccessibilityFocusChainRequester() = default;
  virtual ~AccessibilityFocusChainRequester() = default;

  // Changes the Focus Chain to the View pointed by |view_ref|, invoking |callback| when the
  // request is finished processing. The callback receives the return value informaing whether the
  // request was granted or not.
  virtual void ChangeFocusToView(fuchsia::ui::views::ViewRef view_ref,
                                 ChangeFocusToViewCallback callback) = 0;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_FOCUS_CHAIN_ACCESSIBILITY_FOCUS_CHAIN_REQUESTER_H_
