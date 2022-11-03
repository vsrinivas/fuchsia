// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/arena_v2/gesture_arena_v2.h"

#include <lib/syslog/cpp/macros.h>

#include <utility>

#include "fuchsia/ui/input/accessibility/cpp/fidl.h"
#include "src/ui/a11y/lib/gesture_manager/arena_v2/recognizer_v2.h"

namespace a11y {

namespace {

// Convert a `TouchInteractionId` into a triple of `uint32_t`, so that we can
// use it as the key in a `std::set`.
std::tuple<uint32_t, uint32_t, uint32_t> interactionToTriple(
    fuchsia::ui::pointer::TouchInteractionId interaction) {
  return {interaction.device_id, interaction.pointer_id, interaction.interaction_id};
}

}  // namespace

InteractionTrackerV2::InteractionTrackerV2(HeldInteractionCallback callback)
    : callback_(std::move(callback)) {
  FX_DCHECK(callback_);
}

void InteractionTrackerV2::Reset() {
  FX_DCHECK(open_interactions_.empty());
  FX_DCHECK(held_interactions_.empty());

  status_ = ContestStatus::kUnresolved;
  open_interactions_.clear();
  held_interactions_.clear();
}

void InteractionTrackerV2::AcceptInteractions() {
  FX_DCHECK(status_ == ContestStatus::kUnresolved);
  status_ = ContestStatus::kWinnerAssigned;
  NotifyHeldInteractions();
}

void InteractionTrackerV2::RejectInteractions() {
  FX_DCHECK(status_ == ContestStatus::kUnresolved);
  status_ = ContestStatus::kAllLosers;
  NotifyHeldInteractions();

  // We must clear the open interactions, because Scenic may stop sending us
  // events for those interactions once we reject them. (It also may continue
  // in some cases, since the events are sent in batches.)
  //
  // TODO(fxbug.dev/113881): investigate possible issues.
  open_interactions_.clear();
}

void InteractionTrackerV2::OnEvent(
    const fuchsia::ui::pointer::augment::TouchEventWithLocalHit& event) {
  FX_CHECK(event.touch_event.has_pointer_sample());
  const auto& sample = event.touch_event.pointer_sample();
  const auto& interaction = sample.interaction();
  const auto triple = interactionToTriple(interaction);

  switch (sample.phase()) {
    case fuchsia::ui::pointer::EventPhase::ADD:
      FX_DCHECK(open_interactions_.count(triple) == 0);
      open_interactions_.insert(triple);
      break;
    case fuchsia::ui::pointer::EventPhase::CHANGE:
      break;
    case fuchsia::ui::pointer::EventPhase::REMOVE:
    case fuchsia::ui::pointer::EventPhase::CANCEL:
      // If the contest is unresolved, put this interaction "on hold", and
      // fire a callback for it later, when the contest does resolve.
      if (status_ == ContestStatus::kUnresolved) {
        held_interactions_.push_back(interaction);
      }

      open_interactions_.erase(triple);
      break;
  }
}

ContestStatus InteractionTrackerV2::Status() { return status_; }

bool InteractionTrackerV2::HasOpenInteractions() const { return !open_interactions_.empty(); }

void InteractionTrackerV2::NotifyHeldInteractions() {
  FX_DCHECK(status_ != ContestStatus::kUnresolved);

  for (const auto interaction : held_interactions_) {
    callback_(interaction, status_);
  }

  held_interactions_.clear();
}

// Represents a recognizer's participation in the current contest.
//
// The recognizer is able to affect its state so long as it hasn't already called |Accept| or
// |Reject|. The recognizer receives pointer events so long as this |ParticipationToken|
// remains alive and the recognizer hasn't lost the contest.
//
// Keep in mind that |GestureArenaV2| can call all |ParticipationToken| methods, but
// individual recognizers can only use |ParticipationTokenInterface| methods.
class GestureArenaV2::ParticipationToken : public ParticipationTokenInterface {
 public:
  ParticipationToken(fxl::WeakPtr<GestureArenaV2> arena, RecognizerHandle* recognizer)
      : arena_(arena), recognizer_(recognizer), weak_ptr_factory_(this) {
    FX_DCHECK(recognizer_);
  }

  ~ParticipationToken() override {
    Reject();  // no-op if unnecessary
  }

  fxl::WeakPtr<ParticipationToken> GetWeakPtr() { return weak_ptr_factory_.GetWeakPtr(); }

  GestureRecognizerV2* recognizer() const { return recognizer_->recognizer; }

  // |ParticipationTokenInterface|
  void Accept() override {
    if (arena_ && recognizer_->status == RecognizerStatus::kUndecided) {
      recognizer_->status = RecognizerStatus::kAccepted;

      // Once the first recognizer accepts, we know that the interactions
      // in the current contest belong to us.
      if (arena_->interactions_.Status() == ContestStatus::kUnresolved) {
        arena_->interactions_.AcceptInteractions();
      }

      // Do |FinalizeState| last in case it releases this token.
      FinalizeState();
    }
  }

  // |ParticipationTokenInterface|
  void Reject() override {
    if (arena_ && recognizer_->status == RecognizerStatus::kUndecided) {
      recognizer_->status = RecognizerStatus::kRejected;
      weak_ptr_factory_.InvalidateWeakPtrs();
      // |FinalizeState| won't affect us since we didn't accept.
      FinalizeState();
      // On the other hand, do |OnDefeat| last in case it releases this token.
      recognizer()->OnDefeat();
    }
  }

 private:
  void FinalizeState() {
    FX_DCHECK(arena_->undecided_recognizers_);
    --arena_->undecided_recognizers_;
    arena_->TryToResolve();
  }

  fxl::WeakPtr<GestureArenaV2> arena_;

  RecognizerHandle* const recognizer_;

  fxl::WeakPtrFactory<ParticipationToken> weak_ptr_factory_;
};

GestureArenaV2::GestureArenaV2(InteractionTrackerV2::HeldInteractionCallback callback)
    : interactions_(std::move(callback)), weak_ptr_factory_(this) {}

void GestureArenaV2::Add(GestureRecognizerV2* recognizer) {
  // Initialize status to |kRejected| rather than |kUndecided| just for peace of mind for the case
  // where we add while a contest is ongoing. Really, since we use a counter for undecided
  // recognizers, this could be either, just not |kAccepted|.
  recognizers_.push_back({.recognizer = recognizer, .status = RecognizerStatus::kRejected});
}

// Possible |Remove| implementation:
// fxr/c/fuchsia/+/341227/11/src/ui/a11y/lib/gesture_manager/arena/gesture_arena.cc#151

ContestStatus GestureArenaV2::OnEvent(
    const fuchsia::ui::pointer::augment::TouchEventWithLocalHit& event) {
  FX_CHECK(event.touch_event.has_pointer_sample());
  FX_CHECK(!recognizers_.empty()) << "The a11y Gesture arena is listening for pointer events "
                                     "but has no added gesture recognizer.";
  if (IsIdle()) {
    // An idle arena received a new event. Starts a new contest.
    StartNewContest();
  }

  interactions_.OnEvent(event);
  DispatchEvent(event);

  return interactions_.Status();
}

void GestureArenaV2::TryToResolve() {
  if (undecided_recognizers_ == 0) {
    bool winner_assigned = false;
    for (auto& handle : recognizers_) {
      if (handle.status == RecognizerStatus::kAccepted) {
        if (winner_assigned) {
          handle.recognizer->OnDefeat();
        } else {
          winner_assigned = true;
          FX_LOGS(INFO) << "Gesture Arena: " << handle.recognizer->DebugName() << " Won.";
          handle.recognizer->OnWin();
        }
      }
    }

    // If all recognizers reject, we know that the interactions
    // in the current contest do not belong to us.
    if (!winner_assigned) {
      interactions_.RejectInteractions();
    }
  }
}

void GestureArenaV2::DispatchEvent(
    const fuchsia::ui::pointer::augment::TouchEventWithLocalHit& event) {
  for (auto& handle : recognizers_) {
    if (handle.participation_token) {
      handle.recognizer->HandleEvent(event);
    }
  }
}

void GestureArenaV2::StartNewContest() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  interactions_.Reset();

  undecided_recognizers_ = recognizers_.size();

  for (auto& handle : recognizers_) {
    handle.status = RecognizerStatus::kUndecided;
    auto participation_token =
        std::make_unique<ParticipationToken>(weak_ptr_factory_.GetWeakPtr(), &handle);
    handle.participation_token = participation_token->GetWeakPtr();
    handle.recognizer->OnContestStarted(std::move(participation_token));
  }
}

bool GestureArenaV2::IsHeld() const {
  for (const auto& handle : recognizers_) {
    if (handle.participation_token) {
      return true;
    }
  }
  return false;
}

bool GestureArenaV2::IsIdle() const { return !(interactions_.HasOpenInteractions() || IsHeld()); }

}  // namespace a11y
