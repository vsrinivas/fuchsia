// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader.h"

#include <lib/fpromise/promise.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "fuchsia/accessibility/gesture/cpp/fidl.h"
#include "src/ui/a11y/lib/screen_reader/change_range_value_action.h"
#include "src/ui/a11y/lib/screen_reader/change_semantic_level_action.h"
#include "src/ui/a11y/lib/screen_reader/default_action.h"
#include "src/ui/a11y/lib/screen_reader/explore_action.h"
#include "src/ui/a11y/lib/screen_reader/inject_pointer_event_action.h"
#include "src/ui/a11y/lib/screen_reader/linear_navigation_action.h"
#include "src/ui/a11y/lib/screen_reader/recover_a11y_focus_action.h"
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
constexpr char kIncrementRangeValueActionLabel[] = "Increment Range Value Action";
constexpr char kDecrementRangeValueActionLabel[] = "Decrement Range Value Action";
constexpr char kRecoverA11YFocusActionLabel[] = "Recover A11Y Focus Action";
constexpr char kInjectPointerEventActionLabel[] = "Inject Pointer Event Action";

// Returns the appropriate next action based on the semantic level.
std::string NextActionFromSemanticLevel(ScreenReaderContext::SemanticLevel semantic_level) {
  switch (semantic_level) {
    case ScreenReaderContext::SemanticLevel::kDefault:
      return std::string(kNextActionLabel);
    case ScreenReaderContext::SemanticLevel::kAdjustValue:
      return std::string(kIncrementRangeValueActionLabel);
    default:
      // Other semantic levels are not implemented yet, so return an empty action name.
      return std::string("");
  }
  return std::string("");
}

// Returns the appropriate previous action based on the semantic level.
std::string PreviousActionFromSemanticLevel(ScreenReaderContext::SemanticLevel semantic_level) {
  switch (semantic_level) {
    case ScreenReaderContext::SemanticLevel::kDefault:
      return std::string(kPreviousActionLabel);
    case ScreenReaderContext::SemanticLevel::kAdjustValue:
      return std::string(kDecrementRangeValueActionLabel);
    default:
      // Other semantic levels are not implemented yet, so return an empty action name.
      return std::string("");
  }
  return std::string("");
}

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
                           InjectorManagerInterface* injector_manager,
                           GestureListenerRegistry* gesture_listener_registry,
                           TtsManager* tts_manager, bool announce_screen_reader_enabled)
    : ScreenReader(std::move(context), semantics_source, injector_manager,
                   gesture_listener_registry, tts_manager, announce_screen_reader_enabled,
                   std::make_unique<ScreenReaderActionRegistryImpl>()) {}

ScreenReader::ScreenReader(std::unique_ptr<ScreenReaderContext> context,
                           SemanticsSource* semantics_source,
                           InjectorManagerInterface* injector_manager,
                           GestureListenerRegistry* gesture_listener_registry,
                           TtsManager* tts_manager, bool announce_screen_reader_enabled,
                           std::unique_ptr<ScreenReaderActionRegistry> action_registry)
    : context_(std::move(context)),
      gesture_listener_registry_(gesture_listener_registry),
      tts_manager_(tts_manager),
      action_registry_(std::move(action_registry)),
      weak_ptr_factory_(this) {
  action_context_ = std::make_unique<ScreenReaderAction::ActionContext>();
  action_context_->semantics_source = semantics_source;
  action_context_->injector_manager = injector_manager;
  InitializeActions();
  FX_DCHECK(tts_manager_);

  if (announce_screen_reader_enabled) {
    tts_manager->RegisterTTSEngineReadyCallback(
        [this]() { SpeakMessage(fuchsia::intl::l10n::MessageIds::SCREEN_READER_ON_HINT); });
  }

  context_->speaker()->set_epitaph(fuchsia::intl::l10n::MessageIds::SCREEN_READER_OFF_HINT);
}

ScreenReader::~ScreenReader() { tts_manager_->UnregisterTTSEngineReadyCallback(); }

void ScreenReader::BindGestures(a11y::GestureHandler* gesture_handler) {
  // Add gestures with higher priority earlier than gestures with lower priority.
  // Add a three finger Up swipe recognizer. This corresponds to a physical three finger Right
  // swipe.
  bool gesture_bind_status = gesture_handler->BindSwipeAction(
      [this](GestureContext context) {
        ExecuteAction(kThreeFingerRightSwipeActionLabel, std::move(context));
      },
      GestureHandler::kThreeFingerUpSwipe);
  FX_DCHECK(gesture_bind_status);

  // Add a three finger Down swipe recognizer. This corresponds to a physical three finger Left
  // swipe.
  gesture_bind_status = gesture_handler->BindSwipeAction(
      [this](GestureContext context) {
        ExecuteAction(kThreeFingerLeftSwipeActionLabel, std::move(context));
      },
      GestureHandler::kThreeFingerDownSwipe);
  FX_DCHECK(gesture_bind_status);

  // Add a three finger Left swipe recognizer. This corresponds to a physical three finger Up swipe.
  gesture_bind_status = gesture_handler->BindSwipeAction(
      [this](GestureContext context) {
        ExecuteAction(kThreeFingerUpSwipeActionLabel, std::move(context));
      },
      GestureHandler::kThreeFingerLeftSwipe);
  FX_DCHECK(gesture_bind_status);

  // Add a three finger Right swipe recognizer. This corresponds to a physical three finger Down
  // swipe.
  gesture_bind_status = gesture_handler->BindSwipeAction(
      [this](GestureContext context) {
        ExecuteAction(kThreeFingerDownSwipeActionLabel, std::move(context));
      },
      GestureHandler::kThreeFingerRightSwipe);
  FX_DCHECK(gesture_bind_status);

  // Add one finger Down swipe recognizer. This corresponds to a physical one finger Left swipe.
  gesture_bind_status = gesture_handler->BindSwipeAction(
      [this](GestureContext context) {
        auto action_name = PreviousActionFromSemanticLevel(context_->semantic_level());
        ExecuteAction(action_name, std::move(context));
      },
      GestureHandler::kOneFingerDownSwipe);
  FX_DCHECK(gesture_bind_status);

  // Add one finger Up swipe recognizer. This corresponds to a physical one finger Right swipe.
  gesture_bind_status = gesture_handler->BindSwipeAction(
      [this](GestureContext context) {
        auto action_name = NextActionFromSemanticLevel(context_->semantic_level());
        ExecuteAction(action_name, std::move(context));
      },
      GestureHandler::kOneFingerUpSwipe);
  FX_DCHECK(gesture_bind_status);

  // Add one finger Left swipe recognizer. This corresponds to a physical one finger Up swipe.
  gesture_bind_status = gesture_handler->BindSwipeAction(
      [this](GestureContext context) {
        ExecuteAction(kPreviousSemanticLevelActionLabel, std::move(context));
      },
      GestureHandler::kOneFingerLeftSwipe);
  FX_DCHECK(gesture_bind_status);

  // Add one finger Right swipe recognizer. This corresponds to a physical one finger Down swipe.
  gesture_bind_status = gesture_handler->BindSwipeAction(
      [this](GestureContext context) {
        ExecuteAction(kNextSemanticLevelActionLabel, std::move(context));
      },
      GestureHandler::kOneFingerRightSwipe);
  FX_DCHECK(gesture_bind_status);

  // Add OneFingerDoubleTap recognizer.
  gesture_bind_status =
      gesture_handler->BindOneFingerDoubleTapAction([this](GestureContext context) {
        // This simulated tap down / up event is necessary because some of the supported runtimes at
        // the moment do not have an accessibility action to bring up a keyboard when interacting
        // with a text field.
        if (context_->IsTextFieldFocused()) {
          SimulateTapDown(context);
          SimulateTapUp(context);
        }
        // TODO(fxbug.dev/80277): Default action should not be needed after a simulated tap down /
        // up.
        ExecuteAction(kDefaultActionLabel, std::move(context));
      });
  FX_DCHECK(gesture_bind_status);

  // Add MFingerNTapDragRecognizer (1 finger, 2 taps), recognizer.
  gesture_bind_status = gesture_handler->BindMFingerNTapDragAction(
      [this](GestureContext context) { SimulateTapDown(std::move(context)); }, /*on_start*/
      [this](GestureContext context) {
        ExecuteAction(kInjectPointerEventActionLabel, std::move(context));
      }, /*on_update*/
      [this](GestureContext context) { SimulateTapUp(std::move(context)); } /*on_complete*/,
      1u /*num_fingers*/, 2u /*num_taps*/);
  FX_DCHECK(gesture_bind_status);

  // Add OneFingerSingleTap recognizer.
  gesture_bind_status =
      gesture_handler->BindOneFingerSingleTapAction([this](GestureContext context) {
        context_->set_semantic_level(ScreenReaderContext::SemanticLevel::kDefault);
        ExecuteAction(kExploreActionLabel, std::move(context));
      });
  FX_DCHECK(gesture_bind_status);

  // Add OneFingerDrag recognizer.
  gesture_bind_status = gesture_handler->BindOneFingerDragAction(
      [this](GestureContext context) {
        context_->set_semantic_level(ScreenReaderContext::SemanticLevel::kDefault);
        context_->set_mode(ScreenReaderContext::ScreenReaderMode::kContinuousExploration);
      }, /*on_start*/
      [this](GestureContext context) {
        FX_DCHECK(context_->mode() ==
                  ScreenReaderContext::ScreenReaderMode::kContinuousExploration);
        ExecuteAction(kExploreActionLabel, std::move(context));
      }, /*on_update*/
      [this](GestureContext context) {
        FX_DCHECK(context_->mode() ==
                  ScreenReaderContext::ScreenReaderMode::kContinuousExploration);
        context_->set_mode(ScreenReaderContext::ScreenReaderMode::kNormal);
        // At the end of an explore action, if a virtual keyboard is in focus, activate the last
        // touched key.
        if (context_->IsVirtualKeyboardFocused()) {
          ExecuteAction(kDefaultActionLabel, std::move(context));
        }
      } /*on_complete*/);
  FX_DCHECK(gesture_bind_status);

  // Add TwoFingerSingleTap recognizer.
  gesture_bind_status =
      gesture_handler->BindTwoFingerSingleTapAction([this](GestureContext /* unused */) {
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
  action_registry_->AddAction(
      kIncrementRangeValueActionLabel,
      std::make_unique<ChangeRangeValueAction>(
          action_context_.get(), context_.get(),
          ChangeRangeValueAction::ChangeRangeValueActionType::kIncrementAction));
  action_registry_->AddAction(
      kDecrementRangeValueActionLabel,
      std::make_unique<ChangeRangeValueAction>(
          action_context_.get(), context_.get(),
          ChangeRangeValueAction::ChangeRangeValueActionType::kDecrementAction));

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

  action_registry_->AddAction(
      kRecoverA11YFocusActionLabel,
      std::make_unique<RecoverA11YFocusAction>(action_context_.get(), context_.get()));

  action_registry_->AddAction(
      kInjectPointerEventActionLabel,
      std::make_unique<InjectPointerEventAction>(action_context_.get(), context_.get()));
}

bool ScreenReader::ExecuteAction(const std::string& action_name, GestureContext gesture_context) {
  auto* action = action_registry_->GetActionByName(action_name);
  if (!action) {
    return false;
  }
  action->Run(gesture_context);
  return true;
}

void ScreenReader::SpeakMessage(fuchsia::intl::l10n::MessageIds message_id) {
  auto* speaker = context_->speaker();
  auto promise =
      speaker->SpeakMessageByIdPromise(message_id, {.interrupt = true, .save_utterance = false});
  context_->executor()->schedule_task(std::move(promise));
}

void ScreenReader::SpeakMessage(const std::string& message) {
  auto* speaker = context_->speaker();
  fuchsia::accessibility::tts::Utterance utterance;
  utterance.set_message(message);
  auto promise = speaker->SpeakMessagePromise(std::move(utterance),
                                              {.interrupt = true, .save_utterance = false});
  context_->executor()->schedule_task(std::move(promise));
}

fxl::WeakPtr<SemanticsEventListener> ScreenReader::GetSemanticsEventListenerWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void ScreenReader::OnEvent(SemanticsEventInfo event_info) {
  // Process internal semantic events.
  switch (event_info.event_type) {
    case SemanticsEventType::kSemanticTreeUpdated: {
      GestureContext gesture_context;
      if (event_info.view_ref_koid) {
        gesture_context.view_ref_koid = *event_info.view_ref_koid;
      }
      ExecuteAction(kRecoverA11YFocusActionLabel, std::move(gesture_context));
      break;
    }
    case SemanticsEventType::kUnknown:
      break;
  }

  // Process semantic events coming from semantic providers.
  if (!event_info.semantic_event) {
    return;
  }

  if (event_info.semantic_event->is_announce()) {
    const auto& announce = event_info.semantic_event->announce();
    if (announce.has_message()) {
      SpeakMessage(announce.message());
    }
  }
}

void ScreenReader::SimulateTapDown(GestureContext context) {
  // Enable injector for the view that is receiving pointer events.
  action_context_->injector_manager->MarkViewReadyForInjection(context.view_ref_koid, true);
  // When the gesture detects, events are already under way. We need to inject an (ADD) event here
  // to simulate the beginning of the stream that will be injected after this tap down.
  context.last_event_phase = fuchsia::ui::input::PointerEventPhase::ADD;
  ExecuteAction(kInjectPointerEventActionLabel, context);
  context.last_event_phase = fuchsia::ui::input::PointerEventPhase::MOVE;
  ExecuteAction(kInjectPointerEventActionLabel, context);
}

void ScreenReader::SimulateTapUp(GestureContext context) {
  context.last_event_phase = fuchsia::ui::input::PointerEventPhase::REMOVE;
  ExecuteAction(kInjectPointerEventActionLabel, context);

  // End injection for the view.
  action_context_->injector_manager->MarkViewReadyForInjection(context.view_ref_koid, false);
}

}  // namespace a11y
