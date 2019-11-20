// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/arena/gesture_arena.h"

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"

namespace a11y {

namespace {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;

}  // namespace

PointerStreamTracker::PointerStreamTracker(OnStreamHandledCallback on_stream_handled_callback)
    : on_stream_handled_callback_(std::move(on_stream_handled_callback)) {
  FX_DCHECK(on_stream_handled_callback_);
}

void PointerStreamTracker::RejectPointerEvents() {
  InvokePointerEventCallbacks(fuchsia::ui::input::accessibility::EventHandling::REJECTED);
  // It is also necessary to clear the active streams, because as they were rejected, the input
  // system will not send the rest of the stream to us.
  active_streams_.clear();
}

void PointerStreamTracker::ConsumePointerEvents() {
  InvokePointerEventCallbacks(fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
}

void PointerStreamTracker::InvokePointerEventCallbacks(
    fuchsia::ui::input::accessibility::EventHandling handled) {
  for (const auto& kv : pointer_event_callbacks_) {
    const auto [device_id, pointer_id] = kv.first;
    for (uint32_t times = 1; times <= kv.second; ++times) {
      on_stream_handled_callback_(device_id, pointer_id, handled);
    }
  }
  pointer_event_callbacks_.clear();
}

void PointerStreamTracker::OnEvent(const AccessibilityPointerEvent& pointer_event) {
  // Note that at some point we must answer whether the pointer event stream was
  // consumed / rejected. For this reason, for each ADD event we store the
  // callback that will be responsible for signaling how the events were
  // handled. It is important also to mention that for now, is all or nothing:
  // consume or reject all pointer events that were sent to the arena, and not
  // per a pointer event ID basis. Although this can be implemented, there is no
  // use case for this right now.
  const StreamID stream_id(pointer_event.device_id(), pointer_event.pointer_id());
  switch (pointer_event.phase()) {
    case Phase::ADD: {
      pointer_event_callbacks_[stream_id]++;
      active_streams_.insert(stream_id);
      break;
    }
    case Phase::REMOVE:
      active_streams_.erase(stream_id);
      break;
    default:
      break;
  };
}

// Keep in mind that non-|ContestMember| methods are not visible outside of |GestureArena|.
class GestureArena::ArenaContestMember : public ContestMember {
 public:
  ArenaContestMember(fxl::WeakPtr<GestureArena> arena, ArenaMember* arena_member)
      : arena_(arena), arena_member_(arena_member), weak_ptr_factory_(this) {
    FX_DCHECK(arena_member);
  }

  ~ArenaContestMember() override {
    if (arena_) {
      // This may transition us into an idle state, so do it explicitly ahead of member destruction
      // and then check resolution again.
      weak_ptr_factory_.InvalidateWeakPtrs();
      arena_->TryToResolve();
    }
  }

  fxl::WeakPtr<ArenaContestMember> GetWeakPtr() { return weak_ptr_factory_.GetWeakPtr(); }

  ArenaMember* arena_member() const { return arena_member_; }

  bool is_active() const {
    FX_DCHECK(arena_);
    return arena_member_->status != Status::kDefeated;
  }

  // |ContestMember|
  Status status() const override { return arena_ ? arena_member_->status : Status::kObsolete; }

  // |ContestMember|
  bool Accept() override {
    // Get a weak pointer to ourselves in case the recognizer releases us on win.
    auto weak_this = GetWeakPtr();

    if (arena_ && status() == Status::kContending) {
      arena_member_->SetWin();
      if (weak_this) {
        FX_DCHECK(arena_) << "Recognizers should never destroy arenas.";
        arena_->TryToResolve();
      }
    }
    return weak_this && status() == Status::kWinner;
  }

  // |ContestMember|
  void Reject() override {
    if (arena_ && (status() == Status::kContending || status() == Status::kWinner)) {
      // Get a weak pointer to ourselves in case the recognizer releases us on defeat.
      auto weak_this = GetWeakPtr();
      arena_member_->SetDefeat();
      if (weak_this && arena_) {
        arena_->TryToResolve();
      }
    }
  }

 private:
  fxl::WeakPtr<GestureArena> arena_;
  ArenaMember* const arena_member_;

  fxl::WeakPtrFactory<ArenaContestMember> weak_ptr_factory_;
};

void GestureArena::ArenaMember::SetWin() {
  FX_DCHECK(status == ContestMember::Status::kContending);
  FX_LOGS(INFO) << "winning recognizer: " << recognizer->DebugName();
  status = ContestMember::Status::kWinner;
  recognizer->OnWin();
}

void GestureArena::ArenaMember::SetDefeat() {
  FX_LOGS(INFO) << "defeated recognizer: " << recognizer->DebugName();
  status = ContestMember::Status::kDefeated;
  recognizer->OnDefeat();
}

GestureArena::GestureArena(PointerStreamTracker::OnStreamHandledCallback on_stream_handled_callback,
                           EventHandlingPolicy event_handling_policy)
    : streams_(std::move(on_stream_handled_callback)),
      event_handling_policy_(event_handling_policy),
      weak_ptr_factory_(this) {}

void GestureArena::Add(GestureRecognizer* recognizer) {
  FX_CHECK(!streams_.is_active())
      << "Trying to add a new gesture recognizer to an arena which is already active.";
  arena_members_.push_back({.recognizer = recognizer});
}

void GestureArena::OnEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  FX_CHECK(!arena_members_.empty()) << "The a11y Gesture arena is listening for pointer events "
                                       "but has no added gesture recognizer.";
  if (IsIdle()) {
    // An idle arena received a new event. Starts a new contest.
    StartNewContest();
  }

  streams_.OnEvent(pointer_event);
  DispatchEvent(pointer_event);

  TryToResolve();
}

void GestureArena::TryToResolve() {
  if (state_ == State::kEmpty) {
    return;
  }

  auto [winner, contending] = ClassifyArenaMembers();

  if (state_ == State::kAssigned && !winner) {
    // All members have left the arena, including the winner.
    state_ = State::kEmpty;

    // The arena has no members left, but still needs to handle incoming events. That depends on
    // the policy in which it was configured. Although an empty arena is handled when it becomes
    // empty, it is also necessary to continue handling events until the interaction is over.
    HandleEvents(/*consumed_by_member=*/false);
    return;
  }

  if (state_ == State::kContendingInProgress) {
    if (winner) {
      state_ = State::kAssigned;
      // Someone claimed a win, inform everyone else about their defeat.
      for (ArenaMember* member : contending) {
        member->SetDefeat();
      }
    } else if (contending.size() == 1) {
      // When there is no winner and only the last contending is left, it wins.
      state_ = State::kAssigned;
      contending.front()->SetWin();
    }
  }

  if (IsIdle()) {
    if (state_ == State::kAssigned) {
      // The arena has reached the end of an interaction with a winner.
      HandleEvents(/*consumed_by_member=*/true);
    } else {
      FX_DCHECK(state_ == State::kContendingInProgress);

      // The arena has reached the end of an interaction with no winners. Sweep all members and
      // declares the first one to win.
      Sweep();
    }
  }
}

void GestureArena::Reset() {
  FX_CHECK(!streams_.is_active()) << "Trying to reset an arena which has cached pointer events";
  state_ = State::kContendingInProgress;
  contest_members_.clear();
  weak_ptr_factory_.InvalidateWeakPtrs();
}

bool GestureArena::IsHeld() const {
  for (const auto& contest_member : contest_members_) {
    if (contest_member && contest_member->is_active()) {
      return true;
    }
  }
  return false;
}

GestureArena::ClassifiedArenaMembers GestureArena::ClassifyArenaMembers() {
  ClassifiedArenaMembers c;
  for (auto& member : arena_members_) {
    switch (member.status) {
      case ContestMember::Status::kContending:
        c.contending.push_back(&member);
        break;
      case ContestMember::Status::kWinner:
        FX_CHECK(!c.winner) << "A gesture arena can have up to one winner only.";
        c.winner = &member;
        break;
      case ContestMember::Status::kDefeated:
        // No work to do.
        break;
      case ContestMember::Status::kObsolete:
        FX_NOTREACHED() << "Arena should never set members obsolete.";
        break;
    };
  }
  return c;
}

void GestureArena::DispatchEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  auto it = contest_members_.begin(), end = contest_members_.end();
  while (it != end) {
    fxl::WeakPtr<ArenaContestMember>& contest_member = *it;
    if (contest_member && contest_member->is_active()) {
      contest_member->arena_member()->recognizer->HandleEvent(pointer_event);
    }
    // Recheck in case the handler released the member or declared defeat.
    if (contest_member && contest_member->is_active()) {
      ++it;
    } else {
      // Purge if the member has been released.
      std::swap(*it, *--end);
    }
  }
  contest_members_.erase(end, contest_members_.end());
}

void GestureArena::StartNewContest() {
  Reset();
  for (auto& member : arena_members_) {
    member.status = ContestMember::Status::kContending;
    // If we ever allow adding members while active, we'll need to put members in unique_ptrs to
    // allow for vector reallocation.
    auto contest_member =
        std::make_unique<ArenaContestMember>(weak_ptr_factory_.GetWeakPtr(), &member);
    contest_members_.push_back(contest_member->GetWeakPtr());
    member.recognizer->OnContestStarted(std::move(contest_member));
  }
}

void GestureArena::HandleEvents(bool consumed_by_member) {
  if (consumed_by_member || event_handling_policy_ == EventHandlingPolicy::kConsumeEvents) {
    return streams_.ConsumePointerEvents();
  }
  FX_DCHECK(event_handling_policy_ == EventHandlingPolicy::kRejectEvents);
  streams_.RejectPointerEvents();
}

void GestureArena::Sweep() {
  auto [winner, contending] = ClassifyArenaMembers();
  FX_CHECK(!winner) << "Trying to sweep an arena which has a winner.";
  FX_CHECK(!contending.empty()) << "Trying to sweep an arena with no contending members left.";
  auto it = contending.begin();
  (*it)->SetWin();
  ++it;  // All but the first contender are defeated.
  for (; it != contending.end(); ++it) {
    (*it)->SetDefeat();
  }
}

bool GestureArena::IsIdle() const { return !IsHeld() && !streams_.is_active(); }

}  // namespace a11y
