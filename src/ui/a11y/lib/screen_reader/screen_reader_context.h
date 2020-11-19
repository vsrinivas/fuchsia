// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_SCREEN_READER_CONTEXT_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_SCREEN_READER_CONTEXT_H_

#include <fuchsia/accessibility/tts/cpp/fidl.h>
#include <lib/async/cpp/executor.h>

#include <memory>

#include "src/ui/a11y/lib/screen_reader/focus/a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/speaker.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"

namespace a11y {

// ScreenReaderContext class stores the current state of the screen reader which includes the
// currently selected node(via the a11y focus manager) and state(currently selected semantic level).
// This class will be queried by "Actions" to get screen reader information.
class ScreenReaderContext {
 public:
  // Describes Screen Reader possible modes of navigation.
  enum class ScreenReaderMode {
    kNormal,  // Default case.
    // Whether a continuous exploration is in progress. A continuous exploration is a state
    // where an user is exploring the screen (by touch, for example), and is informed of the
    // elements in a11y focus (hearing the tts, for example). When in continuous exploration, if the
    // user stops at a particular semantic node, this node is informed only once, and another update
    // will only come after the user moves to a different node. In contrast, when the user is not in
    // continuous exploration, if the node is explored multiple times, they will always be informed.
    kContinuousExploration,
  };

  // Defines the different semantic levels.
  // A semantic level is a granularity level of navigation that is used to select the appropriate
  // action when the user performs actions of the form next / previous element. In order to select
  // what is the next element, the Screen Reader uses the semantic level to choose the appropriate
  // logic to use.
  enum class SemanticLevel {
    // Linear Navigation defines what will be the next / previous element.
    kNormalNavigation,
    // Adjusts a value in a slider or range control element.
    kAdjustValue,
    // User is navigating by characters of the text.
    kCharacter,
    // User is navigating by the words of the text.
    kWord,
    // User is navigating by the headings of the text.
    kHeader,
    // User is navigating by form controls.
    kFormControl,
  };

  // |a11y_focus_manager| will be owned by this class.
  // |tts_manager| is not kept, and must be valid only during this constructor, where
  // |tts_engine_ptr_| is instantiated.
  explicit ScreenReaderContext(std::unique_ptr<A11yFocusManager> a11y_focus_manager,
                               TtsManager* tts_manager, std::string locale_id = "en-US");

  virtual ~ScreenReaderContext() = default;

  // Returns pointer to A11yFocusManager which stores a11y focus information for screen reader.
  virtual A11yFocusManager* GetA11yFocusManager();

  // Returns the Executor used by the Screen Reader to schedule promises.
  async::Executor* executor() { return &executor_; }

  virtual Speaker* speaker();

  // Sets the Screen Reader current mode.
  void set_mode(ScreenReaderMode mode) { mode_ = mode; }
  ScreenReaderMode mode() const { return mode_; }

  // Sets the Screen Reader semantic level.
  void set_semantic_level(SemanticLevel semantic_level) { semantic_level_ = semantic_level; }
  SemanticLevel semantic_level() const { return semantic_level_; }

  void set_locale_id(const std::string& locale_id) { locale_id_ = locale_id; }
  const std::string& locale_id() const { return locale_id_; }

 protected:
  // For mocks.
  ScreenReaderContext();

 private:
  async::Executor executor_;

  // Stores A11yFocusManager pointer.
  // A11yFocusManager pointer should never be nullptr.
  std::unique_ptr<A11yFocusManager> a11y_focus_manager_;

  // Interface to the engine is owned by this class so that it can build and rebuild the Speaker
  // when the locale changes.
  fuchsia::accessibility::tts::EnginePtr tts_engine_ptr_;

  // Manages speech tasks of this screen reader.
  std::unique_ptr<Speaker> speaker_;

  // Current Screen Reader mode.
  ScreenReaderMode mode_ = ScreenReaderMode::kNormal;

  // Current semantic level.
  SemanticLevel semantic_level_ = SemanticLevel::kNormalNavigation;

  // Unicode BCP-47 Locale Identifier.
  std::string locale_id_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_SCREEN_READER_CONTEXT_H_
