// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_LINEAR_NAVIGATION_ACTION_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_LINEAR_NAVIGATION_ACTION_H_

#include <lib/fit/scope.h>

#include "src/ui/a11y/lib/screen_reader/screen_reader_action.h"

namespace a11y {

// The LinearNavigationAction allows users to navigate in the UI forward and backwards (AKA the next
// / previous element).
class LinearNavigationAction : public ScreenReaderAction {
 public:
  // The direction of the action when navigating.
  enum LinearNavigationDirection { kNextAction, kPreviousAction };

  explicit LinearNavigationAction(ActionContext* action_context,
                                  ScreenReaderContext* screen_reader_context,
                                  LinearNavigationDirection action_type);

  ~LinearNavigationAction() override;

  // Invokes the linear navigation action, navigating to the node following |direction_| to select
  // next / previous.
  void Run(ActionData process_data) override;

 private:
  // Direction of the linear navigation.
  LinearNavigationDirection direction_;
  fit::scope scope_;
};
}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_LINEAR_NAVIGATION_ACTION_H_
