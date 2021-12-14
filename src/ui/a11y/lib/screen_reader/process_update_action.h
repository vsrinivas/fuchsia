// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_PROCESS_UPDATE_ACTION_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_PROCESS_UPDATE_ACTION_H_

#include <lib/fpromise/scope.h>

#include "src/ui/a11y/lib/screen_reader/focus/a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_action.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"

namespace a11y {

// This action is invoked whenever the semantic tree of the node holding the a11y focus is updated.
// It may:
// - Speak the updated node's new value or label, depending on its type.
class ProcessUpdateAction : public ScreenReaderAction {
 public:
  explicit ProcessUpdateAction(ActionContext* action_context,
                               ScreenReaderContext* screen_reader_context);
  ~ProcessUpdateAction() override;

  void Run(GestureContext gesture_context) override;

 private:
  zx::time last_spoken_feedback_;
  fpromise::scope scope_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_PROCESS_UPDATE_ACTION_H_
