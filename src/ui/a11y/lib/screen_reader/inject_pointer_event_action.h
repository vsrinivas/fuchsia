// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_INJECT_POINTER_EVENT_ACTION_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_INJECT_POINTER_EVENT_ACTION_H_

#include "src/ui/a11y/lib/screen_reader/screen_reader_action.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"

namespace a11y {

// The InjectPointerEventAction allows users to send gestures directly to the
// underlying application.
class InjectPointerEventAction : public ScreenReaderAction {
 public:
  explicit InjectPointerEventAction(ActionContext* action_context,
                                    ScreenReaderContext* screen_reader_context);

  ~InjectPointerEventAction() override;

  // Invokes the inject pointer event action, injecting a pointer event into the
  // currently focused view.
  void Run(GestureContext gesture_context) override;
};
}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_INJECT_POINTER_EVENT_ACTION_H_
