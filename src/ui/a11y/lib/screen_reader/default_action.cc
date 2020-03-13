// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/default_action.h"

#include "src/lib/syslog/cpp/logger.h"

namespace a11y {

DefaultAction::DefaultAction(ActionContext* action_context,
                             ScreenReaderContext* screen_reader_context)
    : action_context_(action_context), screen_reader_context_(screen_reader_context) {
  FX_DCHECK(action_context_);
  FX_DCHECK(screen_reader_context_);
}
DefaultAction::~DefaultAction() = default;

void DefaultAction::Run(ActionData process_data) {
  auto a11y_focus = screen_reader_context_->GetA11yFocusManager()->GetA11yFocus();
  if (!a11y_focus) {
    FX_LOGS(INFO) << "No view is in focus.";
    return;
  }

  const auto tree_weak_ptr = GetTreePointer(action_context_, a11y_focus.value().view_ref_koid);
  if (!tree_weak_ptr) {
    return;
  }
  // Call OnAccessibilityActionRequested.
  tree_weak_ptr->PerformAccessibilityAction(
      a11y_focus.value().node_id, fuchsia::accessibility::semantics::Action::DEFAULT,
      [](bool result) { FX_LOGS(INFO) << "Default Action completed with status:" << result; });
}

}  // namespace a11y
