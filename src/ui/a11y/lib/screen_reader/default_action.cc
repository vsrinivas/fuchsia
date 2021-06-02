// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/default_action.h"

#include <lib/syslog/cpp/macros.h>

namespace a11y {

DefaultAction::DefaultAction(ActionContext* action_context,
                             ScreenReaderContext* screen_reader_context)
    : ScreenReaderAction(action_context, screen_reader_context) {}
DefaultAction::~DefaultAction() = default;

void DefaultAction::Run(GestureContext gesture_context) {
  auto a11y_focus = screen_reader_context_->GetA11yFocusManager()->GetA11yFocus();
  if (!a11y_focus) {
    FX_LOGS(INFO) << "No view is in focus.";
    return;
  }

  // Call OnAccessibilityActionRequested.
  action_context_->semantics_source->PerformAccessibilityAction(
      a11y_focus->view_ref_koid, a11y_focus->node_id,
      fuchsia::accessibility::semantics::Action::DEFAULT,
      [](bool result) { FX_LOGS(INFO) << "Default Action completed with status:" << result; });
}

}  // namespace a11y
