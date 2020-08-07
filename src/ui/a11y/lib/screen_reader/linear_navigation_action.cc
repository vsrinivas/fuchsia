// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/linear_navigation_action.h"

#include <lib/fit/bridge.h>
#include <lib/fit/scope.h>
#include <lib/syslog/cpp/macros.h>

#include "fuchsia/accessibility/semantics/cpp/fidl.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"
#include "src/ui/a11y/lib/screen_reader/util/util.h"

namespace a11y {

LinearNavigationAction::LinearNavigationAction(ActionContext* action_context,
                                               ScreenReaderContext* screen_reader_context,
                                               LinearNavigationDirection action_type)
    : ScreenReaderAction(action_context, screen_reader_context), direction_(action_type) {}

LinearNavigationAction::~LinearNavigationAction() = default;

void LinearNavigationAction::Run(ActionData process_data) {
  auto a11y_focus = screen_reader_context_->GetA11yFocusManager()->GetA11yFocus();
  if (!a11y_focus || a11y_focus->view_ref_koid == ZX_KOID_INVALID) {
    auto* speaker = screen_reader_context_->speaker();
    auto promise = speaker->SpeakMessageByIdPromise(fuchsia::intl::l10n::MessageIds::NO_FOCUS_ALERT,
                                                    {.interrupt = true, .save_utterance = false});
    screen_reader_context_->executor()->schedule_task(std::move(promise));
    FX_LOGS(INFO) << "Linear Navigation Action: No view is in focus.";
    return;
  }

  FX_DCHECK(action_context_->semantics_source);

  // Get the new node base on ActionType.
  const fuchsia::accessibility::semantics::Node* new_node;
  switch (direction_) {
    case kNextAction:
      new_node = action_context_->semantics_source->GetNextNode(
          a11y_focus->view_ref_koid, a11y_focus->node_id,
          [](const fuchsia::accessibility::semantics::Node* node) {
            return NodeIsDescribable(node);
          });
      break;
    case kPreviousAction:
      new_node = action_context_->semantics_source->GetPreviousNode(
          a11y_focus->view_ref_koid, a11y_focus->node_id,
          [](const fuchsia::accessibility::semantics::Node* node) {
            return NodeIsDescribable(node);
          });
      break;
    default:
      new_node = nullptr;
      break;
  }

  if (!new_node || !new_node->has_node_id()) {
    return;
  }

  uint32_t new_node_id = new_node->node_id();
  auto promise =
      ExecuteAccessibilityActionPromise(a11y_focus->view_ref_koid, new_node_id,
                                        fuchsia::accessibility::semantics::Action::SHOW_ON_SCREEN)
          .and_then([this, new_node_id, a11y_focus]() mutable {
            return SetA11yFocusPromise(new_node_id, a11y_focus->view_ref_koid);
          })
          .and_then([this, a11y_focus, new_node_id]() mutable {
            return BuildSpeechTaskFromNodePromise(a11y_focus->view_ref_koid, new_node_id);
          })
          // Cancel any promises if this class goes out of scope.
          .wrap_with(scope_);
  auto* executor = screen_reader_context_->executor();
  executor->schedule_task(std::move(promise));
}

}  // namespace a11y
