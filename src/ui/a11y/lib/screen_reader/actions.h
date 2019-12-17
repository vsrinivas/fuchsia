// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_ACTIONS_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_ACTIONS_H_

#include <fuchsia/accessibility/tts/cpp/fidl.h>
#include <fuchsia/ui/input/accessibility/cpp/fidl.h>

#include "src/ui/a11y/lib/semantics/semantics_manager.h"

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
    zx_koid_t koid;
    ::fuchsia::math::PointF local_point;
  };

  // Struct to hold pointers to various services, which will be required to
  // complete an action.
  struct ActionContext {
    a11y::SemanticsManager* semantics_manager;
    fuchsia::accessibility::tts::EnginePtr tts_engine_ptr;
  };

  ScreenReaderAction();
  virtual ~ScreenReaderAction();

  // Action implementations override this method with the necessary method parameters to perform
  // that action.
  virtual void Run(ActionData process_data) = 0;

  // Helper function to get the tree pointer based on ActionContext and ActionData.
  fxl::WeakPtr<::a11y::SemanticTree> GetTreePointer(ActionContext* context, ActionData data);

  // Helper function to call hit testing based on ActionContext and ActionData.
  void ExecuteHitTesting(
      ActionContext* context, ActionData process_data,
      fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback);
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_ACTIONS_H_
