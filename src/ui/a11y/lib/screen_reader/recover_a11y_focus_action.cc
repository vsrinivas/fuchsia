// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/recover_a11y_focus_action.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/a11y/lib/screen_reader/util/util.h"

namespace a11y {

RecoverA11YFocusAction::RecoverA11YFocusAction(ActionContext* action_context,
                                               ScreenReaderContext* screen_reader_context)
    : ScreenReaderAction(action_context, screen_reader_context) {}

RecoverA11YFocusAction::~RecoverA11YFocusAction() = default;

void RecoverA11YFocusAction::Run(GestureContext gesture_context) {
  auto a11y_focus_manager = screen_reader_context_->GetA11yFocusManager();
  FX_DCHECK(a11y_focus_manager);

  auto a11y_focus = a11y_focus_manager->GetA11yFocus();
  if (!a11y_focus) {
    return;
  }

  if (!action_context_->semantics_source->ViewHasSemantics(a11y_focus->view_ref_koid)) {
    a11y_focus_manager->ClearA11yFocus();
    return;
  }

  // Get the node in focus.
  const auto* focused_node = action_context_->semantics_source->GetSemanticNode(
      a11y_focus->view_ref_koid, a11y_focus->node_id);

  if (focused_node) {
    // If the semantic tree has been updated, it's possible that the bounding
    // box of the currently focused node has changed. Therefore, we should
    // redraw highlights.
    a11y_focus_manager->UpdateHighlights(a11y_focus->view_ref_koid, a11y_focus->node_id);

    // the node still exists, we can stop here.
    return;
  }

  // The node is no longer present but the view is still providing semantics.
  // The current strategy is from the root, tries to find the first focusable node and set the focus
  // to it.
  const int starting_id = 0;
  focused_node =
      action_context_->semantics_source->GetSemanticNode(a11y_focus->view_ref_koid, starting_id);
  if (!focused_node) {
    a11y_focus_manager->ClearA11yFocus();
    return;
  }

  // Now, put in focus a node that is describable.
  if (!NodeIsDescribable(focused_node)) {
    focused_node = action_context_->semantics_source->GetNextNode(
        a11y_focus->view_ref_koid, starting_id,
        [](const fuchsia::accessibility::semantics::Node* node) {
          return NodeIsDescribable(node);
        });

    if (!focused_node) {
      // This tree does not have a node that is describable.
      return;
    }
  }

  uint32_t focused_node_id = focused_node->node_id();
  auto promise =
      ExecuteAccessibilityActionPromise(a11y_focus->view_ref_koid, focused_node_id,
                                        fuchsia::accessibility::semantics::Action::SHOW_ON_SCREEN)
          .and_then([this, focused_node_id, a11y_focus]() mutable {
            return SetA11yFocusPromise(a11y_focus->view_ref_koid, focused_node_id);
          })
          // Cancel any promises if this class goes out of scope.
          .wrap_with(scope_);
  auto* executor = screen_reader_context_->executor();
  executor->schedule_task(std::move(promise));
}

}  // namespace a11y
