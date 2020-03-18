// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/explore_action.h"

#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>

#include "src/lib/syslog/cpp/logger.h"

namespace a11y {
namespace {
using fuchsia::accessibility::semantics::Hit;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::tts::Utterance;

}  // namespace
ExploreAction::ExploreAction(ActionContext* context, ScreenReaderContext* screen_reader_context)
    : action_context_(context), screen_reader_context_(screen_reader_context) {}
ExploreAction::~ExploreAction() = default;

fit::promise<Hit> ExploreAction::ExecuteHitTestingPromise(const ActionData& process_data) {
  fit::bridge<Hit> bridge;
  ExecuteHitTesting(action_context_, process_data,
                    [completer = std::move(bridge.completer)](Hit hit) mutable {
                      if (!hit.has_node_id()) {
                        completer.complete_error();
                      }
                      completer.complete_ok(std::move(hit));
                    });

  return bridge.consumer.promise_or(fit::error());
}

fit::promise<Utterance> ExploreAction::BuildUtteranceFromNodeHitPromise(Hit hit,
                                                                        zx_koid_t view_koid) {
  return fit::make_promise(
      [this, hit = std::move(hit), view_koid]() mutable -> fit::result<Utterance> {
        const auto tree_weak_ptr = GetTreePointer(action_context_, view_koid);
        if (!tree_weak_ptr) {
          FX_LOGS(INFO) << "The semantic tree of the View with View Ref Koid = " << view_koid
                        << " is no longer valid.";
          return fit::error();
        }
        const auto node = tree_weak_ptr->GetNode(hit.node_id());
        if (!node) {
          FX_LOGS(INFO) << "ExploreAction: No node found for node id:" << hit.node_id();
          return fit::error();
        }

        if (!node->has_attributes() || !node->attributes().has_label()) {
          FX_LOGS(INFO) << "ExploreAction: Node is missing Label. Nothing to send to TTS.";
          return fit::error();
        }
        Utterance utterance;
        utterance.set_message(node->attributes().label());

        return fit::ok(std::move(utterance));
      });
}

fit::promise<> ExploreAction::EnqueueUtterancePromise(Utterance utterance) {
  fit::bridge<> bridge;
  action_context_->tts_engine_ptr->Enqueue(
      std::move(utterance), [completer = std::move(bridge.completer)](
                                fuchsia::accessibility::tts::Engine_Enqueue_Result result) mutable {
        if (result.is_err()) {
          FX_LOGS(ERROR) << "Error returned while calling tts::Enqueue()";
          completer.complete_error();
        }
        completer.complete_ok();
      });
  return bridge.consumer.promise_or(fit::error());
}

void ExploreAction::Run(ActionData process_data) {
  auto promise =
      ExecuteHitTestingPromise(process_data)
          .and_then([this, view_koid = process_data.current_view_koid](Hit& hit) mutable {
            return BuildUtteranceFromNodeHitPromise(std::move(hit), view_koid);
          })
          .and_then([this](Utterance& utterance) mutable {
            return EnqueueUtterancePromise(std::move(utterance));
          })
          .and_then([this]() {
            // Speaks the enqueued utterance. No need to chain another promise, as this is the last
            // step.
            action_context_->tts_engine_ptr->Speak(
                [](fuchsia::accessibility::tts::Engine_Speak_Result result) {
                  if (result.is_err()) {
                    FX_LOGS(ERROR) << "Error returned while calling tts::Speak()";
                  }
                });
          })
          // Cancel any promises if this class goes out of scope.
          .wrap_with(scope_);
  auto* executor = screen_reader_context_->executor();
  executor->schedule_task(std::move(promise));
}

}  // namespace a11y
