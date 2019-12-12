// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_SCREEN_READER_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_SCREEN_READER_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "src/ui/a11y/lib/gesture_manager/gesture_handler.h"
#include "src/ui/a11y/lib/screen_reader/explore_action.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"

namespace a11y {

// The Fuchsia Screen Reader.
//
// This is the base class for the Fuchsia Screen Reader. It connects to all
// services necessary to make a funcional Screen Reader.
//
// A common loop would be something like:
//   User performes some sort of input (via touch screen for example). The input
//   triggers an Screen Reader action, which then calls the Fuchsia
//   Accessibility APIs. Finally, some output is communicated (via speech, for
//   example).
// TODO(MI4-2546): Rename this class once the final screen reader name exists.
class ScreenReader {
 public:
  // Pointers to Semantics Manager, TTS Manager and Gesture Manager must outlive
  // screen reader. A11y App is responsible for creating these pointers along
  // with Screen Reader object.
  ScreenReader(a11y::SemanticsManager* semantics_manager_impl, a11y::TtsManager* tts_manager);
  ~ScreenReader() = default;

  void BindGestures(a11y::GestureHandler* gesture_handler);

 private:
  // Initializes services TTS Engine and binds actions to gesture manager.
  void InitializeServicesAndAction();

  // Helps finding the appropriate Action based on Action Name and calls Run()
  // for the matched Action.
  // Functions returns false, if no action matches the provided "action_name",
  // returns true if Run() is called.
  bool ExecuteAction(std::string action_name, ScreenReaderAction::ActionData action_data);

  // Maps action names to screen reader actions.
  std::unordered_map<std::string, std::unique_ptr<ScreenReaderAction>> actions_;

  // Stores Action context which is reaquired to build an Action.
  std::unique_ptr<ScreenReaderAction::ActionContext> action_context_;

  // Pointer to TTS Manager.
  a11y::TtsManager* tts_manager_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_SCREEN_READER_H_
