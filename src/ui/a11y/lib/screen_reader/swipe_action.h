// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_SWIPE_ACTION_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_SWIPE_ACTION_H_

#include <lib/fit/scope.h>

#include "src/ui/a11y/lib/screen_reader/screen_reader_action.h"
namespace a11y {

// SwipeAction class implements actions like Next and Previous.
class SwipeAction : public ScreenReaderAction {
 public:
  // Actions supported by this class.
  enum SwipeActionType { kNextAction, kPreviousAction };

  explicit SwipeAction(ActionContext* action_context, ScreenReaderContext* screen_reader_context,
                       SwipeActionType action_type);
  ~SwipeAction() override;

  // This method implements the sequence of events that should happen when Next or Previous
  // action is performed.
  void Run(ActionData process_data) override;

 private:
  // Stores which Swipe action is being handled.
  SwipeActionType action_type_;
  fit::scope scope_;
};
}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_SWIPE_ACTION_H_
