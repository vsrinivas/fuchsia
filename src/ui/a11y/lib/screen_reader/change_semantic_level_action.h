// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_CHANGE_SEMANTIC_LEVEL_ACTION_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_CHANGE_SEMANTIC_LEVEL_ACTION_H_

#include "src/ui/a11y/lib/screen_reader/focus/a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_action.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"

namespace a11y {

class ChangeSemanticLevelAction : public ScreenReaderAction {
 public:
  // The direction this action cycles through the available semantic levels.
  enum class Direction {
    kForward,
    kBackward,
  };

  explicit ChangeSemanticLevelAction(Direction direction, ActionContext* action_context,
                                     ScreenReaderContext* screen_reader_context);
  ~ChangeSemanticLevelAction() override;

  void Run(ActionData process_data) override;

 private:
  // Returns a promise that speaks the |semantic_level|.
  fit::promise<> SpeakSemanticLevelPromise(ScreenReaderContext::SemanticLevel semantic_level);

  Direction direction_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_CHANGE_SEMANTIC_LEVEL_ACTION_H_
