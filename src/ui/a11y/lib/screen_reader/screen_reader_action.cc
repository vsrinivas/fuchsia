// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader_action.h"

#include <lib/fit/bridge.h>

#include "src/lib/syslog/cpp/logger.h"

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

fit::promise<> ScreenReaderAction::EnqueueUtterancePromise(Utterance utterance) {
  fit::bridge<> bridge;
  action_context_->tts_engine_ptr->Enqueue(
      std::move(utterance), [completer = std::move(bridge.completer)](
                                fuchsia::accessibility::tts::Engine_Enqueue_Result result) mutable {
        if (result.is_err()) {
          FX_LOGS(ERROR) << "ScreenReaderAction: Error returned while calling tts::Enqueue().";
          return completer.complete_error();
        }
        completer.complete_ok();
      });
  return bridge.consumer.promise_or(fit::error());
}

fit::promise<Utterance> ScreenReaderAction::BuildUtteranceFromNodePromise(zx_koid_t view_koid,
                                                                          uint32_t node_id) {
  return fit::make_promise([this, node_id, view_koid]() mutable -> fit::result<Utterance> {
    const auto* node = action_context_->semantics_source->GetSemanticNode(view_koid, node_id);
    if (!node) {
      FX_LOGS(INFO) << "ScreenReaderAction: No node found for node id:" << node_id;
      return fit::error();
    }

    if (!node->has_attributes() || !node->attributes().has_label()) {
      FX_LOGS(INFO) << "ScreenReaderAction: Node is missing Label. Nothing to send to TTS.";
      return fit::error();
    }
    Utterance utterance;
    utterance.set_message(node->attributes().label());

    return fit::ok(std::move(utterance));
  });
}

fit::promise<> ScreenReaderAction::CancelTts() {
  fit::bridge<> bridge;
  action_context_->tts_engine_ptr->Cancel(
      [completer = std::move(bridge.completer)]() mutable { completer.complete_ok(); });
  return bridge.consumer.promise_or(fit::error());
}

}  // namespace a11y
