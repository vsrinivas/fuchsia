// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/recover_a11y_focus_action.h"

#include <lib/syslog/cpp/macros.h>

namespace a11y {

RecoverA11YFocusAction::RecoverA11YFocusAction(ActionContext* action_context,
                                               ScreenReaderContext* screen_reader_context)
    : ScreenReaderAction(action_context, screen_reader_context) {}

RecoverA11YFocusAction::~RecoverA11YFocusAction() = default;

void RecoverA11YFocusAction::Run(ActionData process_data) {
  auto a11y_focus = screen_reader_context_->GetA11yFocusManager()->GetA11yFocus();
  if (!a11y_focus) {
    return;
  }

  // Get the node in focus.
  const fuchsia::accessibility::semantics::Node* focussed_node;
  focussed_node = action_context_->semantics_source->GetSemanticNode(a11y_focus->view_ref_koid,
                                                                     a11y_focus->node_id);

  if (focussed_node) {
    // the node still exists, we can stop here.
    return;
  }

  // This focus no longer exists. Clears its old value and waits for a new user interaction (which
  // will set the focus automatically once they try to select sommething).
  screen_reader_context_->GetA11yFocusManager()->ClearA11yFocus();
}

}  // namespace a11y
