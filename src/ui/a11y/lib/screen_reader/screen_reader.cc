// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader.h"

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/screen_reader/default_action.h"

namespace a11y {

ScreenReader::ScreenReader(a11y::SemanticsManager* semantics_manager, a11y::TtsManager* tts_manager)
    : tts_manager_(tts_manager) {
  action_context_ = std::make_unique<ScreenReaderAction::ActionContext>();
  action_context_->semantics_manager = semantics_manager;

  InitializeServicesAndAction();
}

void ScreenReader::BindGestures(a11y::GestureHandler* gesture_handler) {
  // Add gestures with higher priority earlier than gestures with lower priority.
  // Add OneFingerDoubleTap gesture.
  bool gesture_bind_status = gesture_handler->BindOneFingerDoubleTapAction(
      [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
        ScreenReaderAction::ActionData action_data;
        action_data.koid = viewref_koid;
        action_data.local_point = point;
        ExecuteAction("default_action", action_data);
      });
  FX_DCHECK(gesture_bind_status);

  // Add OneFingerSingleTap gesture.
  gesture_bind_status = gesture_handler->BindOneFingerSingleTapAction(
      [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
        ScreenReaderAction::ActionData action_data;
        action_data.koid = viewref_koid;
        action_data.local_point = point;
        ExecuteAction("explore_action", action_data);
      });
  FX_DCHECK(gesture_bind_status);

  // Add OneFingerDrag gesture.
  gesture_bind_status = gesture_handler->BindOneFingerDragAction(
      [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
        ScreenReaderAction::ActionData action_data;
        action_data.koid = viewref_koid;
        action_data.local_point = point;
        ExecuteAction("explore_action", action_data);
      });
  FX_DCHECK(gesture_bind_status);
}

void ScreenReader::InitializeServicesAndAction() {
  // Initialize TTS Engine which will be used for Speaking using TTS.
  // TTS engine is stored in action_context_.
  tts_manager_->OpenEngine(action_context_->tts_engine_ptr.NewRequest(),
                           [](fuchsia::accessibility::tts::TtsManager_OpenEngine_Result result) {
                             if (result.is_err()) {
                               FX_LOGS(ERROR) << "Tts Manager failed to Open Engine.";
                             }
                           });

  // Initialize Screen reader supported "Actions".
  actions_.insert(std::make_pair("explore_action",
                                 std::make_unique<a11y::ExploreAction>(action_context_.get())));
  actions_.insert(std::make_pair("default_action",
                                 std::make_unique<a11y::DefaultAction>(action_context_.get())));
}

bool ScreenReader::ExecuteAction(const std::string& action_name,
                                 ScreenReaderAction::ActionData action_data) {
  auto action_pair = actions_.find(action_name);

  if (action_pair == actions_.end()) {
    FX_LOGS(ERROR) << "No action found with string :" << action_name;
    return false;
  }

  action_pair->second->Run(action_data);
  return true;
}

}  // namespace a11y
