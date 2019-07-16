// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_ACTIONS_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_ACTIONS_H_

namespace a11y {

// Base class to implement Screen Reader actions.
//
// This is the base class in which all Screen Reader actions depend upon. An
// action is bound to an input (gesture, keyboard shortcut, braille display
// keys, etc), and is triggered whenever that input happens. An action may call
// the Fuchsia Accessibility APIs and / or produce some type of output (Tts, for
// example). This is achieved by accessing information available to this action
// through the context, which is passed in the constructor.
class ScreenReaderAction {
 public:
  ScreenReaderAction() = default;
  virtual ~ScreenReaderAction() = default;
  // Action implementations override this method with the necessary method parameters to perform
  // that action.
  virtual void Run() = 0;
};
}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_ACTIONS_H_
