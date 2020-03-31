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

fxl::WeakPtr<::a11y::SemanticTree> ScreenReaderAction::GetTreePointer(ActionContext* context,
                                                                      zx_koid_t koid) {
  FXL_DCHECK(context);

  return context->view_manager->GetTreeByKoid(koid);
}

void ScreenReaderAction::ExecuteHitTesting(
    ActionContext* context, ActionData process_data,
    fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) {
  FXL_DCHECK(context);
  const auto tree_weak_ptr = GetTreePointer(context, process_data.current_view_koid);
  if (!tree_weak_ptr) {
    return;
  }

  tree_weak_ptr->PerformHitTesting(process_data.local_point, std::move(callback));
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
    const auto tree_weak_ptr = GetTreePointer(action_context_, view_koid);
    if (!tree_weak_ptr) {
      FX_LOGS(INFO) << "The semantic tree of the View with View Ref Koid = " << view_koid
                    << " is no longer valid.";
      return fit::error();
    }
    const auto* node = tree_weak_ptr->GetNode(node_id);
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

}  // namespace a11y
