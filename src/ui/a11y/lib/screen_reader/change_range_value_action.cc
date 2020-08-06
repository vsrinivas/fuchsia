// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/change_range_value_action.h"

#include "fuchsia/accessibility/semantics/cpp/fidl.h"
#include "src/ui/a11y/lib/screen_reader/default_action.h"

namespace a11y {
namespace {

using fuchsia::accessibility::semantics::Action;

}  // namespace

ChangeRangeValueAction::ChangeRangeValueAction(ActionContext* action_context,
                                               ScreenReaderContext* screen_reader_context,
                                               ChangeRangeValueActionType action)

    : ScreenReaderAction(action_context, screen_reader_context), range_value_action_(action) {}

ChangeRangeValueAction::~ChangeRangeValueAction() = default;

void ChangeRangeValueAction::Run(ActionData process_data) {
  auto a11y_focus = screen_reader_context_->GetA11yFocusManager()->GetA11yFocus();
  if (!a11y_focus) {
    FX_LOGS(INFO) << "Change Range Value Action: No view is in focus.";
    return;
  }

  FX_DCHECK(action_context_->semantics_source);

  // Get the node in focus.
  const fuchsia::accessibility::semantics::Node* focussed_node;
  focussed_node = action_context_->semantics_source->GetSemanticNode(a11y_focus->view_ref_koid,
                                                                     a11y_focus->node_id);

  if (!focussed_node || !focussed_node->has_node_id()) {
    return;
  }

  Action semantic_action;
  switch (range_value_action_) {
    case ChangeRangeValueActionType::kDecrementAction:
      semantic_action = Action::DECREMENT;
      break;
    case ChangeRangeValueActionType::kIncrementAction:
      semantic_action = Action::INCREMENT;
      break;
    default:
      break;
  }

  auto promise = ExecuteAccessibilityActionPromise(a11y_focus->view_ref_koid, a11y_focus->node_id,
                                                   semantic_action)
                     .and_then([this, a11y_focus]() mutable {
                       return BuildSpeechTaskForRangeValuePromise(a11y_focus->view_ref_koid,
                                                                  a11y_focus->node_id);
                     })
                     // Cancel any promises if this class goes out of scope.
                     .wrap_with(scope_);

  auto* executor = screen_reader_context_->executor();
  executor->schedule_task(std::move(promise));
}

}  // namespace a11y
