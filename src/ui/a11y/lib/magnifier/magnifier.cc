// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/magnifier/magnifier.h"

#include <lib/async/default.h>

#include <fbl/algorithm.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/lib/ui/input/gesture.h"
#include "src/ui/a11y/lib/gesture_manager/util.h"

namespace a11y {

bool Magnifier::Trigger::ShouldTrigger(input::GestureDetector::TapType tap_type) const {
  return (tap_type == 3 && primer_type_ == PrimerType::k2x3) ||
         (tap_type == 1 && primer_type_ == PrimerType::k3x1_2);
}

bool Magnifier::Trigger::CanTrigger(input::GestureDetector::TapType tap_type) const {
  return tap_type <= 3;
}

void Magnifier::Trigger::OnTapCommit(input::GestureDetector::TapType tap_type) {
  switch (tap_type) {
    case 3:
      primer_type_ = PrimerType::k2x3;
      break;
    case 1:
      switch (primer_type_) {
        case PrimerType::k3x1_1:
          primer_type_ = PrimerType::k3x1_2;
        case PrimerType::k3x1_2:
          break;
        default:
          primer_type_ = PrimerType::k3x1_1;
      }
      break;
    default:
      Reset();
  }
}

void Magnifier::Trigger::Reset() { primer_type_ = PrimerType::kNotPrimed; }

class Magnifier::Interaction : public input::GestureDetector::Interaction {
 public:
  Interaction(Magnifier* view, const input::Gesture* gesture)
      : view_(view),
        // This assumes that we won't receive a win prior to the last interaction. If this weren't
        // the case, we'd probably want to tie |pending_state_| to the |contest member_| or
        // subsequent interactions wouldn't route to the then-committed state.
        affected_state_(view->pending_state_),
        gesture_(gesture),
        temporary_zoom_hold_([this] { make_zoom_persistent_ = false; }),
        weak_ptr_factory_(this) {}

  ~Interaction() override {
    if (is_zoom_temporary_) {
      view_->TransitionOutOfZoom(affected_state_);
    }

    if (!trigger()->is_primed()) {
      view_->contest_member_.reset();
    }
  }

  // Returns whether this interaction can become a pan/zoom gesture, i.e. whether the view is
  // magnified and more than one pointer has been involved.
  bool CanDrag() const { return view_->is_magnified(affected_state_) && manipulation_requested_; }

  fxl::WeakPtr<Interaction> GetWeakPtr() { return weak_ptr_factory_.GetWeakPtr(); }

 private:
  Trigger* trigger() { return &view_->trigger_; }
  async::TaskBase* reset_taps() { return &view_->reset_taps_; }

  ContestMember* contest_member() {
    FX_DCHECK(view_->contest_member_);
    return view_->contest_member_.get();
  }

  // Returns true if the tap was conclusively accepted or rejected.
  bool PerformTapChecks() {
    if (trigger()->ShouldTrigger(tap_type_)) {
      ToggleMagnification();
      AcceptGesture();
      return true;
    } else {
      if (tap_type_ > 1) {
        manipulation_requested_ = true;
      }

      if (!(CanDrag() || trigger()->CanTrigger(tap_type_))) {
        RejectGesture();
        return true;
      }
    }

    return false;
  }

  // |GestureDetector::Interaction|
  void OnTapBegin(const glm::vec2& coordinate, input::GestureDetector::TapType tap_type) override {
    tap_coordinate_ = coordinate;
    tap_type_ = tap_type;

    if (!PerformTapChecks()) {
      reset_taps()->Cancel();
      reset_taps()->PostDelayed(async_get_default_dispatcher(), kTriggerMaxDelay);
    }
  }

  // |GestureDetector::Interaction|
  void OnTapUpdate(input::GestureDetector::TapType tap_type) override {
    tap_type_ = tap_type;
    PerformTapChecks();
  }

  // |GestureDetector::Interaction|
  void OnTapCommit() override {
    if (trigger()->ShouldTrigger(tap_type_)) {
      temporary_zoom_hold_.Cancel();
      if (make_zoom_persistent_) {
        is_zoom_temporary_ = false;
      }

      // Prevents unpleasantly surprising alternation between magnified and not
      // magnified when extra taps happen.
      trigger()->Reset();
    } else {
      trigger()->OnTapCommit(tap_type_);
      if (!(CanDrag() || trigger()->is_primed())) {
        RejectGesture();
      }
    }
  }

  // |GestureDetector::Interaction|
  void OnMultidrag(input::GestureDetector::TapType tap_type,
                   const input::Gesture::Delta& delta) override {
    trigger()->Reset();
    temporary_zoom_hold_.Cancel();

    if (CanDrag()) {
      // display scaling
      float& scale = affected_state_->magnified_scale;
      const float old_scale = scale;
      scale *= delta.scale;
      scale = fbl::clamp(scale, kMinScale, kMaxScale);
      // account for clamping for accurate anchor calculation
      const float actual_delta_scale = scale / old_scale;

      auto& translation = affected_state_->magnified_translation;
      if (is_zoom_temporary_) {
        // If the zoom is temporary, treat the coordinate as a focal point, i.e.
        // focus on the area that would be at that position unzoomed.
        //
        // Instead of using the raw centroid coordinate, which jumps around as
        // fingers are added or removed, move the original tap coordinate by the
        // delta.
        tap_coordinate_ += delta.translation;
        affected_state_->FocusOn(tap_coordinate_);
      } else {
        // Otherwise pan by delta.
        // To anchor the scaling about the centroid, we need to capture the
        // translation of the centroid in the scaled space.
        translation +=
            delta.translation + (translation - gesture_->centroid()) * (actual_delta_scale - 1);
      }

      const float freedom = scale - 1;
      translation.x = fbl::clamp(translation.x, -freedom, freedom);
      translation.y = fbl::clamp(translation.y, -freedom, freedom);

      view_->UpdateIfActive(affected_state_);

      AcceptGesture();
    } else {
      RejectGesture();
    }
  }

  void ToggleMagnification() {
    if (view_->is_magnified(affected_state_)) {
      view_->TransitionOutOfZoom(affected_state_);
    } else {
      is_zoom_temporary_ = true;  // If we start panning, treat as temporary.
      temporary_zoom_hold_.PostDelayed(async_get_default_dispatcher(), kTemporaryZoomHold);
      affected_state_->FocusOn(tap_coordinate_);
      view_->TransitionIntoZoom(affected_state_);
      manipulation_requested_ = true;
    }
  }

  // Caution: this may result in this |Interaction| being freed due to arena defeat. Members should
  // not be accessed after this executes.
  void AcceptGesture() {
    reset_taps()->Cancel();
    contest_member()->Accept();
  }

  // Caution: this may result in this |Interaction| being freed due to arena defeat. Members should
  // not be accessed after this executes.
  void RejectGesture() {
    // Notably it's easier if this happens before |Reject| in case |Reject| frees this
    // |Interaction|.
    reset_taps()->Cancel();
    contest_member()->Reject();
  }

  Magnifier* const view_;
  ControlState* const affected_state_;
  glm::vec2 tap_coordinate_;
  input::GestureDetector::TapType tap_type_;
  const input::Gesture* gesture_;

  // Indicates that changes effected by this interaction should be aligned with a temporary zoom
  // gesture.
  bool is_zoom_temporary_ = false;
  // Indicates that a tap commit should trigger persistent magnification.
  bool make_zoom_persistent_ = true;
  // Indicates that a pan/zoom gesture is active. This needs to be its own boolean rather than
  // derived from tap type and other state because although normally this is triggered by a two-
  // finger tap that can transition into a one-finger pan, this can also be triggered as a
  // continuation of a one-finger triple-tap.
  bool manipulation_requested_ = false;

  async::TaskClosure temporary_zoom_hold_;

  fxl::WeakPtrFactory<Interaction> weak_ptr_factory_;
};  // namespace a11y

Magnifier::Magnifier() : gesture_detector_(this, kDragThreshold), reset_taps_(this) {}

Magnifier::~Magnifier() = default;

void Magnifier::RegisterHandler(
    fidl::InterfaceHandle<fuchsia::accessibility::MagnificationHandler> handler) {
  handler_scope_.Reset();
  update_in_progress_ = update_pending_ = false;
  handler_ = handler.Bind();
  UpdateTransform();
}

void Magnifier::ZoomOutIfMagnified() {
  if (is_magnified(current_state_)) {
    TransitionOutOfZoom(current_state_);
  }
}

void Magnifier::OnWin() {
  std::swap(current_state_, pending_state_);
  if (*current_state_ != *pending_state_) {
    // Checking whether we need to update before firing off an update improves transition
    // responsiveness during trigger by one frame.
    UpdateTransform();
  }
}

void Magnifier::OnDefeat() {
  // Indicate that we don't want to receive further events until the next contest.
  ResetRecognizer();
}

void Magnifier::OnContestStarted(std::unique_ptr<ContestMember> contest_member) {
  contest_member_ = std::move(contest_member);
  *pending_state_ = *current_state_;
}

void Magnifier::HandleEvent(const fuchsia::ui::input::accessibility::PointerEvent& event) {
  gesture_detector_.OnPointerEvent(ToPointerEvent(event));
}

std::string Magnifier::DebugName() const { return "Magnifier"; }

bool Magnifier::ControlState::operator==(const ControlState& o) const {
  return transition_rate == o.transition_rate && magnified_scale == o.magnified_scale &&
         magnified_translation == o.magnified_translation;
}

bool Magnifier::ControlState::operator!=(const ControlState& o) const { return !(*this == o); }

void Magnifier::ControlState::FocusOn(const glm::vec2& focus) {
  magnified_translation = -focus * (magnified_scale - 1);
}

std::unique_ptr<input::GestureDetector::Interaction> Magnifier::BeginInteraction(
    const input::Gesture* gesture) {
  auto interaction = std::make_unique<Interaction>(this, gesture);
  interaction_ = interaction->GetWeakPtr();
  return interaction;
}

void Magnifier::ResetRecognizer() {
  reset_taps_.Cancel();
  ResetTaps();
  gesture_detector_.Reset();
}

void Magnifier::ResetTaps() {
  trigger_.Reset();
  // Don't let the tap timeout interrupt drags that haven't started moving yet.
  //
  // The implications of this logic are actually a bit involved, and hopefully will be simplified by
  // factoring out individual magnification recognizers. If instead we were to accept any |CanDrag|
  // before it starts moving, we would no longer be able to cancel a 2x3 tap with too long a delay
  // since the first 3-tap would satisfy |CanDrag|. Conversely if we reject here even if |CanDrag|,
  // we would reject potential drags that happen to not start moving before the timeout.
  if (!(interaction_ && interaction_->CanDrag())) {
    contest_member_.reset();
  }
}

void Magnifier::UpdateTransform() {
  float& transition_rate = current_state_->transition_rate;

  if (!handler_) {
    // If there's no handler, don't bother animating.
    if (transition_rate > 0) {
      transition_progress_ = 1;
      transition_rate = 0;
    } else if (transition_rate < 0) {
      transition_progress_ = 0;
      transition_rate = 0;
    }
    return;
  }

  if (update_in_progress_) {
    update_pending_ = true;  // We'll |UpdateTransform| on the next callback instead.
  } else {
    update_in_progress_ = true;

    if (transition_rate != 0) {
      transition_progress_ = fbl::clamp(transition_progress_ + transition_rate, 0.f, 1.f);

      if ((transition_rate > 0 && transition_progress_ < 1) ||
          (transition_rate < 0 && transition_progress_ > 0)) {
        update_pending_ = true;
      } else {
        transition_rate = 0;
      }
    }

    handler_->SetClipSpaceTransform(
        transition_progress_ * current_state_->magnified_translation.x,
        transition_progress_ * current_state_->magnified_translation.y,
        1 + transition_progress_ * (current_state_->magnified_scale - 1),
        handler_scope_.MakeScoped([this] {
          update_in_progress_ = false;
          if (update_pending_) {
            update_pending_ = false;
            UpdateTransform();
          }
        }));
  }
}

void Magnifier::UpdateIfActive(const ControlState* state) {
  if (state == current_state_) {
    UpdateTransform();
  }
}

void Magnifier::TransitionIntoZoom(ControlState* state) {
  state->transition_rate = kTransitionRate;
  UpdateIfActive(state);
}

void Magnifier::TransitionOutOfZoom(ControlState* state) {
  state->transition_rate = -kTransitionRate;
  UpdateIfActive(state);
}

bool Magnifier::is_magnified(const ControlState* state) const {
  // The view should be treated as magnified if a transition is underway. A transition can be
  // underway without progress having been made yet if the transition was started while another
  // transform update was already in progress.
  return transition_progress_ > 0 || state->transition_rate > 0;
}

}  // namespace a11y
