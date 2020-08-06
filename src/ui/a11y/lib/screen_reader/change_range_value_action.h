// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_CHANGE_RANGE_VALUE_ACTION_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_CHANGE_RANGE_VALUE_ACTION_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <lib/fit/scope.h>

#include "src/ui/a11y/lib/screen_reader/screen_reader_action.h"

namespace a11y {

// ChangeRangeValueAction class implements increment and decrement action for range control.
class ChangeRangeValueAction : public ScreenReaderAction {
 public:
  // Actions supported by this class.
  enum ChangeRangeValueActionType { kIncrementAction, kDecrementAction };

  explicit ChangeRangeValueAction(ActionContext* action_context,
                                  ScreenReaderContext* screen_reader_context,
                                  ChangeRangeValueActionType action);

  virtual ~ChangeRangeValueAction();

  // This method implements the sequence of events that should happen when range control is
  // incremented or decremented.
  void Run(ActionData process_data) override;

 private:
  // Stores if the range value will be incremented or decremented.
  ChangeRangeValueActionType range_value_action_;

  fit::scope scope_;
};
}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_CHANGE_RANGE_VALUE_ACTION_H_
