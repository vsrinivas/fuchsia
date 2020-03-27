// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_ACTIONS_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_ACTIONS_H_

#include <fuchsia/accessibility/tts/cpp/fidl.h>
#include <fuchsia/ui/input/accessibility/cpp/fidl.h>

#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"
#include "src/ui/a11y/lib/view/view_manager.h"

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
  // Struct for holding data which is required to perform any action.
  struct ActionData {
    zx_koid_t current_view_koid;
    ::fuchsia::math::PointF local_point;
  };

  // Struct to hold pointers to various services, which will be required to
  // complete an action.
  struct ActionContext {
    a11y::ViewManager* view_manager;
    fuchsia::accessibility::tts::EnginePtr tts_engine_ptr;
  };

  explicit ScreenReaderAction(ActionContext* context, ScreenReaderContext* screen_reader_context);
  virtual ~ScreenReaderAction();

  // Action implementations override this method with the necessary method parameters to perform
  // that action.
  virtual void Run(ActionData process_data) = 0;

 protected:
  // Helper function to get the tree pointer based on ActionContext and view koid.
  fxl::WeakPtr<::a11y::SemanticTree> GetTreePointer(ActionContext* context, zx_koid_t koid);

  // Helper function to call hit testing based on ActionContext and ActionData.
  void ExecuteHitTesting(
      ActionContext* context, ActionData process_data,
      fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback);

  // Returns a promise that from a node_id and view_koid, builds an utterance to be spoken. An error
  // is thrown if the semantic tree or the semantic node are missing data necessary to build an
  // utterance.
  fit::promise<fuchsia::accessibility::tts::Utterance> BuildUtteranceFromNodePromise(
      zx_koid_t view_koid, uint32_t node_id);

  // Returns a promise that enqueues an utterance. An error is thrown if the atempt to enqueue the
  // utterance is rejected by the TTS service.
  fit::promise<> EnqueueUtterancePromise(fuchsia::accessibility::tts::Utterance utterance);

  // ActionContext which is used to make calls to Semantics Manager and TTS.
  ActionContext* action_context_;

  // Pointer to the screen reader context, which owns the executor used by this class.
  ScreenReaderContext* screen_reader_context_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_ACTIONS_H_
