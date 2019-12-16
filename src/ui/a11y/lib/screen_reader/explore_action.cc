// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/explore_action.h"

#include "src/lib/syslog/cpp/logger.h"

namespace a11y {

ExploreAction::ExploreAction(ActionContext* context) : action_context_(context) {}
ExploreAction::~ExploreAction() = default;

void ExploreAction::ProcessHitTestResult(::fuchsia::accessibility::semantics::Hit hit,
                                         ActionData process_data) {
  if (hit.has_node_id()) {
    const auto tree_weak_ptr = action_context_->semantics_manager->GetTreeByKoid(process_data.koid);
    if (!tree_weak_ptr) {
      FX_LOGS(INFO) << "The semantic tree of the View with View Ref Koid = " << process_data.koid
                    << " is no longer valid.";
      return;
    }
    const auto node = tree_weak_ptr->GetNode(hit.node_id());
    if (!node) {
      FX_LOGS(INFO) << "ExploreAction: No node found for node id:" << hit.node_id();
      return;
    }

    if (!node->has_attributes() || !node->attributes().has_label()) {
      FX_LOGS(INFO) << "ExploreAction: Node is missing Label. Nothing to send to TTS.";
      return;
    }
    fuchsia::accessibility::tts::Utterance utter;
    utter.set_message(node->attributes().label());
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
  const auto tree_weak_ptr = action_context_->semantics_manager->GetTreeByKoid(process_data.koid);
  if (!tree_weak_ptr) {
    FX_LOGS(INFO) << "The semantic tree of the View with View Ref Koid = " << process_data.koid
                  << " is no longer valid.";
    return;
  }
  tree_weak_ptr->PerformHitTesting(
      process_data.local_point, [this, process_data](::fuchsia::accessibility::semantics::Hit hit) {
        ProcessHitTestResult(std::move(hit), process_data);
      });
}

}  // namespace a11y
