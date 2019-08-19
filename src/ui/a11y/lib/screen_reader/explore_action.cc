// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/explore_action.h"

#include <lib/syslog/cpp/logger.h>

namespace a11y {

ExploreAction::ExploreAction(std::shared_ptr<ActionContext> context) : action_context_(context) {}

void ExploreAction::ProcessHitTestResult(::fuchsia::accessibility::semantics::Hit hit,
                                         ActionData process_data) {
  if (hit.has_node_id()) {
    fuchsia::accessibility::semantics::NodePtr node_ptr =
        action_context_->semantics_manager->GetAccessibilityNodeByKoid(process_data.koid,
                                                                       hit.node_id());
    if (node_ptr == nullptr) {
      FX_LOGS(INFO) << "ExploreAction: No node found for node id:" << hit.node_id();
      return;
    }

    if (!node_ptr->has_attributes() || !node_ptr->attributes().has_label()) {
      FX_LOGS(INFO) << "ExploreAction: Node is missing Label. Nothing to send to TTS.";
      return;
    }
    fuchsia::accessibility::tts::Utterance utter;
    utter.set_message(node_ptr->attributes().label());
    action_context_->tts_engine_ptr->Enqueue(
        std::move(utter), [](fuchsia::accessibility::tts::Engine_Enqueue_Result result) {
          if (result.is_err()) {
            FX_LOGS(ERROR) << "Error returned while calling tts::Enqueue()";
          }
        });
    action_context_->tts_engine_ptr->Speak(
        [](fuchsia::accessibility::tts::Engine_Speak_Result result) {
          if (result.is_err()) {
            FX_LOGS(ERROR) << "Error returned while calling tts::Speak()";
          }
        });
  } else {
    FX_LOGS(INFO) << "ExploreAction: Node id is missing in the result.";
  }
}

void ExploreAction::Run(ActionData process_data) {
  action_context_->semantics_manager->PerformHitTesting(
      process_data.koid, process_data.local_point,
      [this, process_data](::fuchsia::accessibility::semantics::Hit hit) {
        ProcessHitTestResult(std::move(hit), process_data);
      });
}

}  // namespace a11y
