// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_EXPLORE_ACTION_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_EXPLORE_ACTION_H_

#include <lib/fit/promise.h>
#include <lib/fit/scope.h>

#include "src/ui/a11y/lib/screen_reader/screen_reader_action.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"

namespace a11y {

// This class implements "Explore" Action. Explore Action is triggered when the
// user touches an element on the screen to see what is under the finger.
// Responsibilities of Explore Actions are:
//   * Given a touch point and view koid, figure out which node is touched.
//   * If a node is being touched, then with the help of TTS read out the label.
//   * Manage focus change for a node(if any).
class ExploreAction : public ScreenReaderAction {
 public:
  explicit ExploreAction(ActionContext* context, ScreenReaderContext* screen_reader_context);
  ~ExploreAction() override;

  // This method will be implementing the actual sequence of events that should
  // happen when an element is "explored".
  void Run(ActionData process_data) override;

 private:
  // Returns a promise which performs hit testing. An error is thrown if the hit test result has no
  // node ID.
  fit::promise<fuchsia::accessibility::semantics::Hit> ExecuteHitTestingPromise(
      const ActionData& process_data);

  fit::scope scope_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_EXPLORE_ACTION_H_
