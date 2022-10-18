// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/arena_v2/gesture_arena_v2.h"

#include <lib/syslog/cpp/macros.h>

#include <utility>

#include "src/ui/a11y/lib/gesture_manager/arena_v2/recognizer_v2.h"

namespace a11y {

namespace {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;

}  // namespace

InteractionTracker::InteractionTracker(OnInteractionHandledCallback on_interaction_handled_callback)
    : on_interaction_handled_callback_(std::move(on_interaction_handled_callback)) {
  FX_DCHECK(on_interaction_handled_callback_);
}

void InteractionTracker::Reset() {
  handled_.reset();
  pointer_event_callbacks_.clear();
  open_interactions_.clear();
}

void InteractionTracker::RejectPointerEvents() {
  InvokePointerEventCallbacks(fuchsia::ui::input::accessibility::EventHandling::REJECTED);
  // It is also necessary to clear the open interactions, because as they were rejected,
  // Scenic will not send us the remaining events from those interactions.
  open_interactions_.clear();
}

void InteractionTracker::ConsumePointerEvents() {
  InvokePointerEventCallbacks(fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
}

void InteractionTracker::InvokePointerEventCallbacks(
    fuchsia::ui::input::accessibility::EventHandling handled) {
  handled_ = handled;

  for (const auto& kv : pointer_event_callbacks_) {
    const auto [device_id, pointer_id] = kv.first;
    for (uint32_t times = 1; times <= kv.second; ++times) {
      on_interaction_handled_callback_(device_id, pointer_id, handled);
    }
  }
  pointer_event_callbacks_.clear();
}

void InteractionTracker::OnEvent(const AccessibilityPointerEvent& pointer_event) {
  // Note that at some point we must answer whether the interaction was
  // consumed / rejected. For this reason, for each ADD event we store the
  // callback that will be responsible for signaling how that interaction was
  // handled.
  //
  // It's worth mentioning that our handling is "all or nothing": we either
  // consume or reject all events in an interaction. We also either consume
  // all interactions, or reject all interactions, until the tracker is reset.
  const InteractionID interaction_id(pointer_event.device_id(), pointer_event.pointer_id());
  switch (pointer_event.phase()) {
    case Phase::ADD: {
      if (handled_) {
        on_interaction_handled_callback_(pointer_event.device_id(), pointer_event.pointer_id(),
                                         *handled_);
      } else {
        pointer_event_callbacks_[interaction_id]++;
      }
      open_interactions_.insert(interaction_id);
      break;
    }
    case Phase::REMOVE:
      open_interactions_.erase(interaction_id);
      break;
    default:
      break;
  };
}

// Represents a contest member in an arena.
//
// The member is able to affect its state so long as the arena exists and |Accept| or |Reject| has
// not already been called. The associated recognizer receives pointer events so long as this
// |ContestMemberV2| remains alive and not defeated.
//
// Keep in mind that non-|ContestMemberV2| methods are not visible outside of |GestureArenaV2|.
class GestureArenaV2::ArenaContestMember : public ContestMemberV2 {
 public:
  ArenaContestMember(fxl::WeakPtr<GestureArenaV2> arena, ArenaMember* arena_member)
      : arena_(arena), arena_member_(arena_member), weak_ptr_factory_(this) {
    FX_DCHECK(arena_member_);
  }

  ~ArenaContestMember() override {
    Reject();  // no-op if unnecessary
  }

  fxl::WeakPtr<ArenaContestMember> GetWeakPtr() { return weak_ptr_factory_.GetWeakPtr(); }

  GestureRecognizerV2* recognizer() const { return arena_member_->recognizer; }

  // |ContestMemberV2|
  void Accept() override {
    if (arena_ && arena_member_->status == Status::kUndecided) {
      arena_member_->status = Status::kAccepted;
      arena_->HandleEvents(true);
      // Do |FinalizeState| last in case it releases this member.
      FinalizeState();
    }
  }

  // |ContestMemberV2|
  void Reject() override {
    if (arena_ && arena_member_->status == Status::kUndecided) {
      arena_member_->status = Status::kRejected;
      weak_ptr_factory_.InvalidateWeakPtrs();
      // |FinalizeState| won't affect us since we didn't claim a win.
      FinalizeState();
      // On the other hand, do |OnDefeat| last in case it releases this member.
      recognizer()->OnDefeat();
    }
  }

 private:
  void FinalizeState() {
    FX_DCHECK(arena_->undecided_members_);
    --arena_->undecided_members_;
    arena_->TryToResolve();
  }

  fxl::WeakPtr<GestureArenaV2> arena_;
  ArenaMember* const arena_member_;

  fxl::WeakPtrFactory<ArenaContestMember> weak_ptr_factory_;
};

GestureArenaV2::GestureArenaV2(
    InteractionTracker::OnInteractionHandledCallback on_interaction_handled_callback)
    : interactions_(std::move(on_interaction_handled_callback)), weak_ptr_factory_(this) {}

void GestureArenaV2::Add(GestureRecognizerV2* recognizer) {
  // Initialize status to |kRejected| rather than |kUndecided| just for peace of mind for the case
  // where we add while active. Really, since we use a counter for undecided members, this could be
  // either, just not |kAccepted|.
  arena_members_.push_back(
      {.recognizer = recognizer, .status = ContestMemberV2::Status::kRejected});
}

// Possible |Remove| implementation:
// fxr/c/fuchsia/+/341227/11/src/ui/a11y/lib/gesture_manager/arena/gesture_arena.cc#151

void GestureArenaV2::OnEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  FX_CHECK(!arena_members_.empty()) << "The a11y Gesture arena is listening for pointer events "
                                       "but has no added gesture recognizer.";
  if (IsIdle()) {
    // An idle arena received a new event. Starts a new contest.
    StartNewContest();
  }

  interactions_.OnEvent(pointer_event);
  DispatchEvent(pointer_event);
}

void GestureArenaV2::TryToResolve() {
  if (undecided_members_ == 0) {
    bool winner_assigned = false;
    for (auto& member : arena_members_) {
      if (member.status == ContestMemberV2::Status::kAccepted) {
        if (winner_assigned) {
          member.recognizer->OnDefeat();
        } else {
          winner_assigned = true;
          FX_LOGS(INFO) << "Gesture Arena: " << member.recognizer->DebugName() << " Won.";
          member.recognizer->OnWin();
        }
      }
    }

    if (!winner_assigned) {
      HandleEvents(false);
    }
  }
}

GestureArenaV2::State GestureArenaV2::GetState() {
  FX_DCHECK(false) << "not yet implemented";
  return State::kInProgress;
}

void GestureArenaV2::DispatchEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  for (auto& member : arena_members_) {
    if (member.contest_member) {
      member.recognizer->HandleEvent(pointer_event);
    }
  }
}

void GestureArenaV2::StartNewContest() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  interactions_.Reset();

  undecided_members_ = arena_members_.size();

  for (auto& member : arena_members_) {
    member.status = ContestMemberV2::Status::kUndecided;
    auto contest_member =
        std::make_unique<ArenaContestMember>(weak_ptr_factory_.GetWeakPtr(), &member);
    member.contest_member = contest_member->GetWeakPtr();
    member.recognizer->OnContestStarted(std::move(contest_member));
  }
}

void GestureArenaV2::HandleEvents(bool consumed_by_member) {
  if (consumed_by_member) {
    interactions_.ConsumePointerEvents();
  } else {
    interactions_.RejectPointerEvents();
  }
}

bool GestureArenaV2::IsHeld() const {
  for (const auto& member : arena_members_) {
    if (member.contest_member) {
      return true;
    }
  }
  return false;
}

bool GestureArenaV2::IsIdle() const { return !(interactions_.is_active() || IsHeld()); }

}  // namespace a11y
