// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/explore_action.h"

#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/syslog/cpp/macros.h>

#include <cstdint>

#include "src/ui/a11y/lib/screen_reader/screen_reader.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_action.h"
#include "src/ui/a11y/lib/screen_reader/util/util.h"

namespace a11y {
namespace {
using fuchsia::accessibility::semantics::Hit;
using fuchsia::accessibility::tts::Utterance;

}  // namespace

ExploreAction::ExploreAction(ActionContext* context, ScreenReaderContext* screen_reader_context)
    : ScreenReaderAction(context, screen_reader_context) {}
ExploreAction::~ExploreAction() = default;

fpromise::promise<Hit> ExploreAction::ExecuteHitTestingPromise(
    const GestureContext& gesture_context) {
  fpromise::bridge<Hit> bridge;
  ExecuteHitTesting(action_context_, gesture_context,
                    [completer = std::move(bridge.completer)](Hit hit) mutable {
                      if (!hit.has_node_id()) {
                        return completer.complete_error();
                      }
                      completer.complete_ok(std::move(hit));
                    });

  return bridge.consumer.promise_or(fpromise::error());
}

fpromise::result<uint32_t> ExploreAction::SelectDescribableNodePromise(zx_koid_t view_koid,
                                                                       Hit& hit) {
  if (!hit.has_node_id()) {
    return fpromise::error();
  }

  auto hit_test_result_node =
      action_context_->semantics_source->GetSemanticNode(view_koid, hit.node_id());
  if (!hit_test_result_node || !hit_test_result_node->has_node_id()) {
    FX_LOGS(WARNING) << "Invalid hit test result.";
    return fpromise::error();
  }

  auto node_to_return = hit_test_result_node;
  while (node_to_return && !NodeIsDescribable(node_to_return)) {
    FX_DCHECK(node_to_return->has_node_id());
    node_to_return =
        action_context_->semantics_source->GetParentNode(view_koid, node_to_return->node_id());
  }

  if (node_to_return && node_to_return->has_node_id()) {
    return fpromise::ok(node_to_return->node_id());
  }

  FX_LOGS(WARNING) << "No describable ancestor found for node " << hit_test_result_node->node_id();
  return fpromise::error();
}

fpromise::promise<> ExploreAction::SetA11yFocusOrStopPromise(
    ScreenReaderContext::ScreenReaderMode mode, zx_koid_t view_koid, uint32_t node_id) {
  return fpromise::make_promise([this, mode, view_koid, node_id]() mutable -> fpromise::promise<> {
    if (mode == ScreenReaderContext::ScreenReaderMode::kContinuousExploration) {
      // If the new a11y focus to be set is the same as the existing one during a
      // continuous exploration, this means that the same node would be spoken
      // multiple times. Check if the focus is new before continuing.
      auto* a11y_focus_manager = screen_reader_context_->GetA11yFocusManager();
      auto focus = a11y_focus_manager->GetA11yFocus();
      if (!focus) {
        return fpromise::make_error_promise();
      }
      if (focus->view_ref_koid == view_koid && focus->node_id == node_id) {
        return fpromise::make_error_promise();
      }
    }
    return SetA11yFocusPromise(node_id, view_koid);
  });
}

void ExploreAction::Run(GestureContext gesture_context) {
  auto promise =
      ExecuteHitTestingPromise(gesture_context)
          .and_then([this, view_koid = gesture_context.view_ref_koid](
                        Hit& hit) mutable -> fpromise::result<uint32_t> {
            return SelectDescribableNodePromise(view_koid, hit);
          })
          .and_then([this, view_koid = gesture_context.view_ref_koid,
                     mode = screen_reader_context_->mode()](
                        uint32_t& node_id) mutable -> fpromise::promise<> {
            return SetA11yFocusOrStopPromise(mode, view_koid, node_id);
          })
          .and_then([this]() mutable -> fpromise::result<A11yFocusManager::A11yFocusInfo> {
            auto* a11y_focus_manager = screen_reader_context_->GetA11yFocusManager();
            auto focus = a11y_focus_manager->GetA11yFocus();
            if (!focus) {
              return fpromise::error();
            }
            return fpromise::ok(std::move(*focus));
          })
          .and_then([this](const A11yFocusManager::A11yFocusInfo& focus) mutable {
            return BuildSpeechTaskFromNodePromise(focus.view_ref_koid, focus.node_id);
          })
          // Cancel any promises if this class goes out of scope.
          .wrap_with(scope_);
  auto* executor = screen_reader_context_->executor();
  executor->schedule_task(std::move(promise));
}

}  // namespace a11y
