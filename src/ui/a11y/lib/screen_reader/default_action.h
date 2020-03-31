// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_DEFAULT_ACTION_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_DEFAULT_ACTION_H_

#include "src/ui/a11y/lib/screen_reader/focus/a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_action.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"

namespace a11y {
// This class implements "Default" Action. Default Action is triggered when the
// user double taps an element on the screen to perform default action associated with the element.
// Responsibilities of Default Action is:
//   * Given a touch point and view koid, call OnAccessibilityActionRequested on the semantic
//   listener for default action.
class DefaultAction : public ScreenReaderAction {
 public:
  explicit DefaultAction(ActionContext* action_context, ScreenReaderContext* screen_reader_context);
  ~DefaultAction() override;

  // This method implements the actual sequence of events that should
  // happen when an associated gesture is performed on an element.
  void Run(ActionData process_data) override;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_DEFAULT_ACTION_H_
