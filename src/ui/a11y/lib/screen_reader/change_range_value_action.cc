// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/change_range_value_action.h"

#include "fuchsia/accessibility/semantics/cpp/fidl.h"
#include "src/ui/a11y/lib/screen_reader/default_action.h"
#include "src/ui/a11y/lib/screen_reader/util/util.h"

namespace a11y {
namespace {

using fuchsia::accessibility::semantics::Action;

}  // namespace

ChangeRangeValueAction::ChangeRangeValueAction(ActionContext* action_context,
                                               ScreenReaderContext* screen_reader_context,
                                               ChangeRangeValueActionType action)

    : ScreenReaderAction(action_context, screen_reader_context), range_value_action_(action) {}

ChangeRangeValueAction::~ChangeRangeValueAction() = default;

void ChangeRangeValueAction::Run(GestureContext gesture_context) {
  auto a11y_focus = screen_reader_context_->GetA11yFocusManager()->GetA11yFocus();
  if (!a11y_focus) {
    FX_LOGS(INFO) << "Change Range Value Action: No view is in focus.";
    return;
  }

  FX_DCHECK(action_context_->semantics_source);

  // Get the node in focus.
  const zx_koid_t focused_koid = a11y_focus->view_ref_koid;
  const uint32_t focused_node_id = a11y_focus->node_id;

  const fuchsia::accessibility::semantics::Node* focused_node;
  focused_node = action_context_->semantics_source->GetSemanticNode(focused_koid, focused_node_id);

  if (!focused_node || !focused_node->has_node_id()) {
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

  auto old_value = GetSliderValue(*focused_node);

  auto promise =
      ExecuteAccessibilityActionPromise(a11y_focus->view_ref_koid, a11y_focus->node_id,
                                        semantic_action)
          .and_then([this, focused_koid, focused_node_id, old_value]() {
            // Ask the screen reader to read the next value of the slider.
            screen_reader_context_->set_on_node_update_callback([this, focused_koid,
                                                                 focused_node_id, old_value]() {
              // Get current focus.
              auto a11y_focus = screen_reader_context_->GetA11yFocusManager()->GetA11yFocus();
              if (!a11y_focus) {
                return;
              }

              // If the focused node has changed, then we shouldn't
              // try to read the new slider value.
              if (a11y_focus->view_ref_koid != focused_koid ||
                  a11y_focus->node_id != focused_node_id) {
                return;
              }

              auto new_focused_node = action_context_->semantics_source->GetSemanticNode(
                  a11y_focus->view_ref_koid, a11y_focus->node_id);

              // If the focused node no longer exists, then we
              // shouldn't try to read the new slider value.
              if (!new_focused_node) {
                return;
              }

              // If the slider value hasn't changed, or is no longer
              // valid, then we shouldn't try to read it.
              auto new_value = GetSliderValue(*new_focused_node);
              if (new_value == old_value || new_value.empty()) {
                return;
              }

              // Read the new slider value.
              auto* speaker = screen_reader_context_->speaker();
              FX_DCHECK(speaker);

              fuchsia::accessibility::tts::Utterance utterance;
              utterance.set_message(new_value);
              auto promise = speaker->SpeakMessagePromise(std::move(utterance), {.interrupt = true})
                                 .wrap_with(scope_);
              auto* executor = screen_reader_context_->executor();
              executor->schedule_task(std::move(promise));
            });
            return fpromise::make_ok_promise();
          })
          // Cancel any promises if this class goes out of scope.
          .wrap_with(scope_);

  auto* executor = screen_reader_context_->executor();
  executor->schedule_task(std::move(promise));
}

}  // namespace a11y
