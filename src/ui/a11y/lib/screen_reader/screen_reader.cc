// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader.h"

#include <lib/fit/promise.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "fuchsia/accessibility/gesture/cpp/fidl.h"
#include "src/ui/a11y/lib/screen_reader/change_semantic_level_action.h"
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
constexpr char kPreviousSemanticLevelActionLabel[] = "Previous Semantic Level Action";
constexpr char kNextSemanticLevelActionLabel[] = "Next Semantic Level Action";

}  // namespace

// Private implementation of the registry for the Screen Reader use only. Note that only the Screen
// Reader will be able to access the methods implemented here.
class ScreenReader::ScreenReaderActionRegistryImpl : public ScreenReaderActionRegistry {
 public:
  ScreenReaderActionRegistryImpl() = default;
  ~ScreenReaderActionRegistryImpl() override = default;
  void AddAction(std::string name, std::unique_ptr<ScreenReaderAction> action) override {
    actions_.insert({std::move(name), std::move(action)});
  }

  ScreenReaderAction* GetActionByName(const std::string& name) override {
    auto action_it = actions_.find(name);
    if (action_it == actions_.end()) {
      FX_LOGS(ERROR) << "No Screen Reader action found with string :" << name;
      return nullptr;
    }
    return action_it->second.get();
  }

 private:
  std::unordered_map<std::string, std::unique_ptr<ScreenReaderAction>> actions_;
};

ScreenReader::ScreenReader(std::unique_ptr<ScreenReaderContext> context,
                           SemanticsSource* semantics_source,
                           GestureListenerRegistry* gesture_listener_registry)
    : ScreenReader(std::move(context), semantics_source, gesture_listener_registry,
                   std::make_unique<ScreenReaderActionRegistryImpl>()) {}

ScreenReader::ScreenReader(std::unique_ptr<ScreenReaderContext> context,
                           a11y::SemanticsSource* semantics_source,
                           a11y::GestureListenerRegistry* gesture_listener_registry,
                           std::unique_ptr<ScreenReaderActionRegistry> action_registry)
    : context_(std::move(context)),
      gesture_listener_registry_(gesture_listener_registry),
      action_registry_(std::move(action_registry)) {
  action_context_ = std::make_unique<ScreenReaderAction::ActionContext>();
  action_context_->semantics_source = semantics_source;
  InitializeActions();
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

  // Add one finger Left swipe recognizer. This corresponds to a physical one finger Up swipe.
  gesture_bind_status = gesture_handler->BindSwipeAction(
      [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
        ScreenReaderAction::ActionData action_data;
        action_data.current_view_koid = viewref_koid;
        action_data.local_point = point;
        ExecuteAction(kPreviousSemanticLevelActionLabel, action_data);
      },
      GestureHandler::kOneFingerLeftSwipe);
  FX_DCHECK(gesture_bind_status);

  // Add one finger Right swipe recognizer. This corresponds to a physical one finger Down swipe.
  gesture_bind_status = gesture_handler->BindSwipeAction(
      [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
        ScreenReaderAction::ActionData action_data;
        action_data.current_view_koid = viewref_koid;
        action_data.local_point = point;
        ExecuteAction(kNextSemanticLevelActionLabel, action_data);
      },
      GestureHandler::kOneFingerRightSwipe);
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

void ScreenReader::InitializeActions() {
  action_registry_->AddAction(kExploreActionLabel, std::make_unique<a11y::ExploreAction>(
                                                       action_context_.get(), context_.get()));
  action_registry_->AddAction(kDefaultActionLabel, std::make_unique<a11y::DefaultAction>(
                                                       action_context_.get(), context_.get()));
  action_registry_->AddAction(
      kPreviousActionLabel,
      std::make_unique<a11y::LinearNavigationAction>(
          action_context_.get(), context_.get(), a11y::LinearNavigationAction::kPreviousAction));
  action_registry_->AddAction(kNextActionLabel, std::make_unique<a11y::LinearNavigationAction>(
                                                    action_context_.get(), context_.get(),
                                                    a11y::LinearNavigationAction::kNextAction));
  action_registry_->AddAction(
      kNextSemanticLevelActionLabel,
      std::make_unique<ChangeSemanticLevelAction>(ChangeSemanticLevelAction::Direction::kForward,
                                                  action_context_.get(), context_.get()));
  action_registry_->AddAction(
      kPreviousSemanticLevelActionLabel,
      std::make_unique<ChangeSemanticLevelAction>(ChangeSemanticLevelAction::Direction::kBackward,
                                                  action_context_.get(), context_.get()));

  action_registry_->AddAction(kThreeFingerUpSwipeActionLabel,
                              std::make_unique<a11y::ThreeFingerSwipeAction>(
                                  action_context_.get(), context_.get(), gesture_listener_registry_,
                                  fuchsia::accessibility::gesture::Type::THREE_FINGER_SWIPE_UP));

  action_registry_->AddAction(kThreeFingerDownSwipeActionLabel,
                              std::make_unique<a11y::ThreeFingerSwipeAction>(
                                  action_context_.get(), context_.get(), gesture_listener_registry_,
                                  fuchsia::accessibility::gesture::Type::THREE_FINGER_SWIPE_DOWN));

  action_registry_->AddAction(kThreeFingerLeftSwipeActionLabel,
                              std::make_unique<a11y::ThreeFingerSwipeAction>(
                                  action_context_.get(), context_.get(), gesture_listener_registry_,
                                  fuchsia::accessibility::gesture::Type::THREE_FINGER_SWIPE_LEFT));

  action_registry_->AddAction(kThreeFingerRightSwipeActionLabel,
                              std::make_unique<a11y::ThreeFingerSwipeAction>(
                                  action_context_.get(), context_.get(), gesture_listener_registry_,
                                  fuchsia::accessibility::gesture::Type::THREE_FINGER_SWIPE_RIGHT));
}

bool ScreenReader::ExecuteAction(const std::string& action_name,
                                 ScreenReaderAction::ActionData action_data) {
  auto* action = action_registry_->GetActionByName(action_name);
  if (!action) {
    return false;
  }
  action->Run(action_data);
  return true;
}

}  // namespace a11y
