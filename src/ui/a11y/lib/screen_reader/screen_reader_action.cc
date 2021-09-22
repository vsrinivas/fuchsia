// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader_action.h"

#include <lib/fpromise/bridge.h>
#include <lib/syslog/cpp/macros.h>

#include <string>

#include "fuchsia/accessibility/semantics/cpp/fidl.h"
#include "src/ui/a11y/lib/screen_reader/util/util.h"

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
    ActionContext* context, GestureContext gesture_context,
    fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) {
  FX_DCHECK(context);
  FX_DCHECK(context->semantics_source);
  context->semantics_source->ExecuteHitTesting(
      gesture_context.view_ref_koid, gesture_context.CurrentCentroid(true /* local coordinates */),
      std::move(callback));
}

fpromise::promise<> ScreenReaderAction::ExecuteAccessibilityActionPromise(
    zx_koid_t view_ref_koid, uint32_t node_id, fuchsia::accessibility::semantics::Action action) {
  fpromise::bridge<> bridge;
  action_context_->semantics_source->PerformAccessibilityAction(
      view_ref_koid, node_id, action,
      [completer = std::move(bridge.completer)](bool handled) mutable {
        if (!handled) {
          return completer.complete_error();
        }
        completer.complete_ok();
      });
  return bridge.consumer.promise_or(fpromise::error());
}

fpromise::promise<> ScreenReaderAction::SetA11yFocusPromise(const uint32_t node_id,
                                                            zx_koid_t view_koid) {
  fpromise::bridge<> bridge;
  auto* a11y_focus_manager = screen_reader_context_->GetA11yFocusManager();
  a11y_focus_manager->SetA11yFocus(view_koid, node_id,
                                   [completer = std::move(bridge.completer)](bool success) mutable {
                                     if (!success) {
                                       return completer.complete_error();
                                     }
                                     completer.complete_ok();
                                   });
  return bridge.consumer.promise_or(fpromise::error());
}

fpromise::promise<> ScreenReaderAction::BuildSpeechTaskFromNodePromise(zx_koid_t view_koid,
                                                                       uint32_t node_id) {
  return fpromise::make_promise([this, node_id, view_koid]() mutable -> fpromise::promise<> {
    const auto* node = action_context_->semantics_source->GetSemanticNode(view_koid, node_id);
    if (!node) {
      FX_LOGS(INFO) << "ScreenReaderAction: No node found for node id:" << node_id;
      return fpromise::make_error_promise();
    }

    auto* speaker = screen_reader_context_->speaker();
    FX_DCHECK(speaker);
    if (screen_reader_context_->IsVirtualKeyboardFocused()) {
      // Read the key in the virtual keyboard.
      return speaker->SpeakNodeCanonicalizedLabelPromise(node, {.interrupt = true});
    }

    // When not focusing a virtual keyboard node, just describe the node.
    return speaker->SpeakNodePromise(node, {.interrupt = true});
  });
}

fpromise::promise<> ScreenReaderAction::BuildSpeechTaskForRangeValuePromise(zx_koid_t view_koid,
                                                                            uint32_t node_id) {
  return fpromise::make_promise([this, node_id, view_koid]() mutable -> fpromise::promise<> {
    const auto* node = action_context_->semantics_source->GetSemanticNode(view_koid, node_id);
    if (!node) {
      FX_LOGS(INFO) << "ScreenReaderAction: No node found for node id:" << node_id;
      return fpromise::make_error_promise();
    }

    std::string slider_value = GetSliderValue(*node);
    if (slider_value.empty()) {
      FX_LOGS(INFO) << "ScreenReaderAction: Slider node is missing |range_value| and |value|. "
                       "Nothing to send to TTS.";
      return fpromise::make_error_promise();
    }

    auto* speaker = screen_reader_context_->speaker();
    FX_DCHECK(speaker);

    Utterance utterance;
    utterance.set_message(slider_value);
    return speaker->SpeakMessagePromise(std::move(utterance), {.interrupt = true});
  });
}

}  // namespace a11y
