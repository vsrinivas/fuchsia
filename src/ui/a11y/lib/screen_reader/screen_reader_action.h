// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_SCREEN_READER_ACTION_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_SCREEN_READER_ACTION_H_

#include <fuchsia/accessibility/tts/cpp/fidl.h>
#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/fit/promise.h>

#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"
#include "src/ui/a11y/lib/semantics/semantics_source.h"
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
    a11y::SemanticsSource* semantics_source;
  };

  explicit ScreenReaderAction(ActionContext* context, ScreenReaderContext* screen_reader_context);
  virtual ~ScreenReaderAction();

  // Action implementations override this method with the necessary method parameters to perform
  // that action.
  virtual void Run(ActionData process_data) = 0;

 protected:
  // Helper function to call hit testing based on ActionContext and ActionData.
  void ExecuteHitTesting(
      ActionContext* context, ActionData process_data,
      fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback);

  // Returns a promise that executes an accessibility action targeting the semantic tree
  // corresponding to |view_ref_koid|, on the node |node_id|. An error is thrown if the semantic
  // tree can't be found or if the semantic provider did not handle this action.
  fit::promise<> ExecuteAccessibilityActionPromise(
      zx_koid_t view_ref_koid, uint32_t node_id, fuchsia::accessibility::semantics::Action action);

  // Returns a promise that sets a new A11y Focus. If the operation is not successful, throws an
  // error.
  fit::promise<> SetA11yFocusPromise(const uint32_t node_id, zx_koid_t view_koid);

  // Returns a promise that from a node_id and view_koid, builds a speech task to speak the node
  // description. An error is thrown if the semantic tree or the semantic node are missing data
  // necessary to build an utterance.
  fit::promise<> BuildSpeechTaskFromNodePromise(zx_koid_t view_koid, uint32_t node_id);

  // Returns a promise that from a node_id and view_koid, builds a speech task to speak the range
  // control's |range_value|. An error is thrown if the semantic tree or the semantic node are
  // missing data necessary to build an utterance.
  fit::promise<> BuildSpeechTaskForRangeValuePromise(zx_koid_t view_koid, uint32_t node_id);

  // ActionContext which is used to make calls to Semantics Manager and TTS.
  ActionContext* action_context_;

  // Pointer to the screen reader context, which owns the executor used by this class.
  ScreenReaderContext* screen_reader_context_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_SCREEN_READER_ACTION_H_
