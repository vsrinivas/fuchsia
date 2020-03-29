// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/explore_action.h"

#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>

#include <cstdint>

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/screen_reader/actions.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader.h"

namespace a11y {
namespace {
using fuchsia::accessibility::semantics::Hit;
using fuchsia::accessibility::tts::Utterance;

}  // namespace

ExploreAction::ExploreAction(ActionContext* context, ScreenReaderContext* screen_reader_context)
    : ScreenReaderAction(context, screen_reader_context) {}
ExploreAction::~ExploreAction() = default;

fit::promise<Hit> ExploreAction::ExecuteHitTestingPromise(const ActionData& process_data) {
  fit::bridge<Hit> bridge;
  ExecuteHitTesting(action_context_, process_data,
                    [completer = std::move(bridge.completer)](Hit hit) mutable {
                      if (!hit.has_node_id()) {
                        return completer.complete_error();
                      }
                      completer.complete_ok(std::move(hit));
                    });

  return bridge.consumer.promise_or(fit::error());
}

void ExploreAction::Run(ActionData process_data) {
  auto promise =
      ExecuteHitTestingPromise(process_data)
          .and_then([this, view_koid = process_data.current_view_koid](Hit& hit) mutable {
            return SetA11yFocusPromise(hit.node_id(), view_koid);
          })
          .and_then([this]() mutable -> fit::result<A11yFocusManager::A11yFocusInfo> {
            auto* a11y_focus_manager = screen_reader_context_->GetA11yFocusManager();
            auto focus = a11y_focus_manager->GetA11yFocus();
            if (!focus) {
              return fit::error();
            }
            return fit::ok(std::move(*focus));
          })
          .and_then([this](const A11yFocusManager::A11yFocusInfo& focus) mutable {
            return BuildUtteranceFromNodePromise(focus.view_ref_koid, focus.node_id);
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
