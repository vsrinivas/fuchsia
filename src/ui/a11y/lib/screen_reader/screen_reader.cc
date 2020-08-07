// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader.h"

#include <lib/fit/promise.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "fuchsia/accessibility/gesture/cpp/fidl.h"
#include "src/ui/a11y/lib/screen_reader/default_action.h"
#include "src/ui/a11y/lib/screen_reader/explore_action.h"
#include "src/ui/a11y/lib/screen_reader/linear_navigation_action.h"
#include "src/ui/a11y/lib/screen_reader/three_finger_swipe_action.h"

namespace a11y {
namespace {
constexpr char kNextActionLabel[] = "Next Action";
constexpr char kPreviousActionLabel[] = "Previous Action";
constexpr char kExploreActionLabel[] = "Explore Action";
constexpr char kDefaultActionLabel[] = "Default Action";
constexpr char kThreeFingerUpSwipeActionLabel[] = "Three finger Up Swipe Action";
constexpr char kThreeFingerDownSwipeActionLabel[] = "Three finger Down Swipe Action";
constexpr char kThreeFingerLeftSwipeActionLabel[] = "Three finger Left Swipe Action";
constexpr char kThreeFingerRightSwipeActionLabel[] = "Three finger Right Swipe Action";
}  // namespace

ScreenReader::ScreenReader(std::unique_ptr<ScreenReaderContext> context,
                           a11y::SemanticsSource* semantics_source,
                           a11y::GestureListenerRegistry* gesture_listener_registry)
    : context_(std::move(context)), gesture_listener_registry_(gesture_listener_registry) {
  action_context_ = std::make_unique<ScreenReaderAction::ActionContext>();
  action_context_->semantics_source = semantics_source;

  InitializeServicesAndAction();
}

void ScreenReader::BindGestures(a11y::GestureHandler* gesture_handler) {
  // Add gestures with higher priority earlier than gestures with lower priority.
  // Add a three finger Up swipe recognizer. This corresponds to a physical three finger Right
  // swipe.
  bool gesture_bind_status = gesture_handler->BindSwipeAction(
      [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
        ScreenReaderAction::ActionData action_data;
        action_data.current_view_koid = viewref_koid;
        action_data.local_point = point;
        ExecuteAction(kThreeFingerRightSwipeActionLabel, action_data);
      },
      GestureHandler::kThreeFingerUpSwipe);
  FX_DCHECK(gesture_bind_status);

  // Add a three finger Down swipe recognizer. This corresponds to a physical three finger Left
  // swipe.
  gesture_bind_status = gesture_handler->BindSwipeAction(
      [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
        ScreenReaderAction::ActionData action_data;
        action_data.current_view_koid = viewref_koid;
        action_data.local_point = point;
        ExecuteAction(kThreeFingerLeftSwipeActionLabel, action_data);
      },
      GestureHandler::kThreeFingerDownSwipe);
  FX_DCHECK(gesture_bind_status);

  // Add a three finger Left swipe recognizer. This corresponds to a physical three finger Up swipe.
  gesture_bind_status = gesture_handler->BindSwipeAction(
      [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
        ScreenReaderAction::ActionData action_data;
        action_data.current_view_koid = viewref_koid;
        action_data.local_point = point;
        ExecuteAction(kThreeFingerUpSwipeActionLabel, action_data);
      },
      GestureHandler::kThreeFingerLeftSwipe);
  FX_DCHECK(gesture_bind_status);

  // Add a three finger Right swipe recognizer. This corresponds to a physical three finger Down
  // swipe.
  gesture_bind_status = gesture_handler->BindSwipeAction(
      [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
        ScreenReaderAction::ActionData action_data;
        action_data.current_view_koid = viewref_koid;
        action_data.local_point = point;
        ExecuteAction(kThreeFingerDownSwipeActionLabel, action_data);
      },
      GestureHandler::kThreeFingerRightSwipe);
  FX_DCHECK(gesture_bind_status);

  // Add one finger Down swipe recognizer. This corresponds to a physical one finger Left swipe.
  gesture_bind_status = gesture_handler->BindSwipeAction(
      [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
        ScreenReaderAction::ActionData action_data;
        action_data.current_view_koid = viewref_koid;
        action_data.local_point = point;
        ExecuteAction(kPreviousActionLabel, action_data);
      },
      GestureHandler::kOneFingerDownSwipe);
  FX_DCHECK(gesture_bind_status);

  // Add one finger Up swipe recognizer. This corresponds to a physical one finger Right swipe.
  gesture_bind_status = gesture_handler->BindSwipeAction(
      [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
        ScreenReaderAction::ActionData action_data;
        action_data.current_view_koid = viewref_koid;
        action_data.local_point = point;
        ExecuteAction(kNextActionLabel, action_data);
      },
      GestureHandler::kOneFingerUpSwipe);
  FX_DCHECK(gesture_bind_status);

  // Add OneFingerDoubleTap recognizer.
  gesture_bind_status = gesture_handler->BindOneFingerDoubleTapAction(
      [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
        ScreenReaderAction::ActionData action_data;
        action_data.current_view_koid = viewref_koid;
        action_data.local_point = point;
        ExecuteAction(kDefaultActionLabel, action_data);
      });
  FX_DCHECK(gesture_bind_status);

  // Add OneFingerSingleTap recognizer.
  gesture_bind_status = gesture_handler->BindOneFingerSingleTapAction(
      [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
        ScreenReaderAction::ActionData action_data;
        action_data.current_view_koid = viewref_koid;
        action_data.local_point = point;
        ExecuteAction(kExploreActionLabel, action_data);
      });
  FX_DCHECK(gesture_bind_status);

  // Add OneFingerDrag recognizer.
  gesture_bind_status = gesture_handler->BindOneFingerDragAction(
      [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
        context_->set_mode(ScreenReaderContext::ScreenReaderMode::kContinuousExploration);
      }, /*on_start*/
      [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
        FX_DCHECK(context_->mode() ==
                  ScreenReaderContext::ScreenReaderMode::kContinuousExploration);
        ScreenReaderAction::ActionData action_data;
        action_data.current_view_koid = viewref_koid;
        action_data.local_point = point;
        ExecuteAction(kExploreActionLabel, action_data);
      }, /*on_update*/
      [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
        FX_DCHECK(context_->mode() ==
                  ScreenReaderContext::ScreenReaderMode::kContinuousExploration);
        context_->set_mode(ScreenReaderContext::ScreenReaderMode::kNormal);
      } /*on_complete*/);
  FX_DCHECK(gesture_bind_status);

  // Add TwoFingerSingleTap recognizer.
  gesture_bind_status = gesture_handler->BindTwoFingerSingleTapAction(
      [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
        // Cancel any outstanding speech.
        auto promise = context_->speaker()->CancelTts();
        auto* executor = context_->executor();
        executor->schedule_task(std::move(promise));
      });
  FX_DCHECK(gesture_bind_status);
}

void ScreenReader::InitializeServicesAndAction() {
  // Initialize Screen reader supported "Actions".
  actions_.insert(std::make_pair(kExploreActionLabel, std::make_unique<a11y::ExploreAction>(
                                                          action_context_.get(), context_.get())));
  actions_.insert(std::make_pair(kDefaultActionLabel, std::make_unique<a11y::DefaultAction>(
                                                          action_context_.get(), context_.get())));
  actions_.insert(std::make_pair(
      kPreviousActionLabel,
      std::make_unique<a11y::LinearNavigationAction>(
          action_context_.get(), context_.get(), a11y::LinearNavigationAction::kPreviousAction)));
  actions_.insert(std::make_pair(kNextActionLabel, std::make_unique<a11y::LinearNavigationAction>(
                                                       action_context_.get(), context_.get(),
                                                       a11y::LinearNavigationAction::kNextAction)));

  actions_.insert(
      std::make_pair(kThreeFingerUpSwipeActionLabel,
                     std::make_unique<a11y::ThreeFingerSwipeAction>(
                         action_context_.get(), context_.get(), gesture_listener_registry_,
                         fuchsia::accessibility::gesture::Type::THREE_FINGER_SWIPE_UP)));

  actions_.insert(
      std::make_pair(kThreeFingerDownSwipeActionLabel,
                     std::make_unique<a11y::ThreeFingerSwipeAction>(
                         action_context_.get(), context_.get(), gesture_listener_registry_,
                         fuchsia::accessibility::gesture::Type::THREE_FINGER_SWIPE_DOWN)));

  actions_.insert(
      std::make_pair(kThreeFingerLeftSwipeActionLabel,
                     std::make_unique<a11y::ThreeFingerSwipeAction>(
                         action_context_.get(), context_.get(), gesture_listener_registry_,
                         fuchsia::accessibility::gesture::Type::THREE_FINGER_SWIPE_LEFT)));

  actions_.insert(
      std::make_pair(kThreeFingerRightSwipeActionLabel,
                     std::make_unique<a11y::ThreeFingerSwipeAction>(
                         action_context_.get(), context_.get(), gesture_listener_registry_,
                         fuchsia::accessibility::gesture::Type::THREE_FINGER_SWIPE_RIGHT)));
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
