// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader_action.h"

#include <lib/fit/bridge.h>
#include <lib/syslog/cpp/macros.h>

#include <string>

#include "fuchsia/accessibility/semantics/cpp/fidl.h"

namespace a11y {
namespace {

using fuchsia::accessibility::tts::Utterance;

}  // namespace

ScreenReaderAction::ScreenReaderAction(ActionContext* context,
                                       ScreenReaderContext* screen_reader_context)
    : action_context_(context), screen_reader_context_(screen_reader_context) {
  FX_DCHECK(action_context_);
  FX_DCHECK(screen_reader_context_);
}

ScreenReaderAction::~ScreenReaderAction() = default;

void ScreenReaderAction::ExecuteHitTesting(
    ActionContext* context, ActionData process_data,
    fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) {
  FX_DCHECK(context);
  FX_DCHECK(context->semantics_source);
  context->semantics_source->ExecuteHitTesting(process_data.current_view_koid,
                                               process_data.local_point, std::move(callback));
}

fit::promise<> ScreenReaderAction::ExecuteAccessibilityActionPromise(
    zx_koid_t view_ref_koid, uint32_t node_id, fuchsia::accessibility::semantics::Action action) {
  fit::bridge<> bridge;
  action_context_->semantics_source->PerformAccessibilityAction(
      view_ref_koid, node_id, action,
      [completer = std::move(bridge.completer)](bool handled) mutable {
        if (!handled) {
          return completer.complete_error();
        }
        completer.complete_ok();
      });
  return bridge.consumer.promise_or(fit::error());
}

fit::promise<> ScreenReaderAction::SetA11yFocusPromise(const uint32_t node_id,
                                                       zx_koid_t view_koid) {
  fit::bridge<> bridge;
  auto* a11y_focus_manager = screen_reader_context_->GetA11yFocusManager();
  a11y_focus_manager->SetA11yFocus(view_koid, node_id,
                                   [completer = std::move(bridge.completer)](bool success) mutable {
                                     if (!success) {
                                       return completer.complete_error();
                                     }
                                     completer.complete_ok();
                                   });
  return bridge.consumer.promise_or(fit::error());
}

fit::promise<> ScreenReaderAction::BuildSpeechTaskFromNodePromise(zx_koid_t view_koid,
                                                                  uint32_t node_id) {
  return fit::make_promise([this, node_id, view_koid]() mutable -> fit::promise<> {
    const auto* node = action_context_->semantics_source->GetSemanticNode(view_koid, node_id);
    if (!node) {
      FX_LOGS(INFO) << "ScreenReaderAction: No node found for node id:" << node_id;
      return fit::make_error_promise();
    }

    if (!node->has_attributes() || !node->attributes().has_label()) {
      FX_LOGS(INFO) << "ScreenReaderAction: Node is missing Label. Nothing to send to TTS.";
      return fit::make_error_promise();
    }
    auto* speaker = screen_reader_context_->speaker();
    FX_DCHECK(speaker);
    return speaker->SpeakNodePromise(node, {.interrupt = true});
  });
}

fit::promise<> ScreenReaderAction::BuildSpeechTaskForRangeValuePromise(zx_koid_t view_koid,
                                                                       uint32_t node_id) {
  return fit::make_promise([this, node_id, view_koid]() mutable -> fit::promise<> {
    const auto* node = action_context_->semantics_source->GetSemanticNode(view_koid, node_id);
    if (!node) {
      FX_LOGS(INFO) << "ScreenReaderAction: No node found for node id:" << node_id;
      return fit::make_error_promise();
    }

    if (!node->has_role() || (node->role() != fuchsia::accessibility::semantics::Role::SLIDER)) {
      FX_LOGS(INFO) << "ScreenReaderAction: Node is not slider. Nothing to send to TTS.";
      return fit::make_error_promise();
    }

    if (!node->has_states() || !node->states().has_range_value()) {
      FX_LOGS(INFO)
          << "ScreenReaderAction: Slider node is missing |range_value|. Nothing to send to TTS.";
      return fit::make_error_promise();
    }

    auto* speaker = screen_reader_context_->speaker();
    FX_DCHECK(speaker);

    Utterance utterance;
    utterance.set_message(std::to_string(static_cast<int>(node->states().range_value())));
    return speaker->SpeakMessagePromise(std::move(utterance), {.interrupt = true});
  });
}

}  // namespace a11y
