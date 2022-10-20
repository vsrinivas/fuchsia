// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_RECOVER_A11Y_FOCUS_ACTION_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_RECOVER_A11Y_FOCUS_ACTION_H_

#include <lib/fpromise/scope.h>

#include "src/ui/a11y/lib/screen_reader/focus/a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_action.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"

namespace a11y {

// This action verifies that the A11Y Focus is still valid. If the node that the focus points to no
// longer exists, the focus is reset.
class RecoverA11YFocusAction : public ScreenReaderAction {
 public:
  explicit RecoverA11YFocusAction(ActionContext* action_context,
                                  ScreenReaderContext* screen_reader_context);
  ~RecoverA11YFocusAction() override;

  void Run(GestureContext gesture_context) override;

 private:
  // Check whether the focus is valid (i.e., the a11y focus exists and the
  // focused node is still present).
  bool FocusIsValid();

  fpromise::scope scope_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_RECOVER_A11Y_FOCUS_ACTION_H_
