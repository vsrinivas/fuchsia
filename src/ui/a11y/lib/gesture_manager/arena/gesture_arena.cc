// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/arena/gesture_arena.h"

#include "src/lib/syslog/cpp/logger.h"

namespace a11y {

namespace {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;

// Holds pointers to arena members in different states.
// If winner is not nullptr, contending is empty. If contending is not empty, winner is nullptr.
struct ClassifiedArenaMembers {
  ArenaMember* winner = nullptr;
  std::vector<ArenaMember*> contending;
};

// Classifies the arena members into winner and contending.
// Example:
// auto [winner, contending] = ClassifyArenaMembers(arena_members);
ClassifiedArenaMembers ClassifyArenaMembers(
    const std::vector<std::unique_ptr<ArenaMember>>& arena_members) {
  ClassifiedArenaMembers c;
  for (const auto& member : arena_members) {
    switch (member->status()) {
      case ArenaMember::Status::kWinner:
        FX_CHECK(!c.winner) << "A gesture arena can have up to one winner only.";
        c.winner = member.get();
        break;
      case ArenaMember::Status::kDefeated:
        // No work to do.
        break;
      case ArenaMember::Status::kContending:
        c.contending.push_back(member.get());
        break;
    };
  }
  return c;
}

}  // namespace

PointerEventRouter::PointerEventRouter(OnStreamHandledCallback on_stream_handled_callback)
    : on_stream_handled_callback_(std::move(on_stream_handled_callback)) {
  FX_DCHECK(on_stream_handled_callback_);
}

void PointerEventRouter::RejectPointerEvents() {
  InvokePointerEventCallbacks(fuchsia::ui::input::accessibility::EventHandling::REJECTED);
  // it is also necessary to clear the active streams, because as they were rejected, the input
  // system will not send the rest of the stream to us.
  active_streams_.clear();
}

void PointerEventRouter::ConsumePointerEvents() {
  InvokePointerEventCallbacks(fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
}

void PointerEventRouter::InvokePointerEventCallbacks(
    fuchsia::ui::input::accessibility::EventHandling handled) {
  for (const auto& kv : pointer_event_callbacks_) {
    const auto [device_id, pointer_id] = kv.first;
    for (uint32_t times = 1; times <= kv.second; ++times) {
      on_stream_handled_callback_(device_id, pointer_id, handled);
    }
  }
  pointer_event_callbacks_.clear();
}

void PointerEventRouter::RouteEvent(
    const AccessibilityPointerEvent& pointer_event,
    const std::vector<std::unique_ptr<ArenaMember>>& arena_members) {
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
  for (const auto& member : arena_members) {
    if (member->is_active()) {
      member->recognizer()->HandleEvent(pointer_event);
    }
  }
}

ArenaMember::ArenaMember(GestureArena* arena, GestureRecognizer* recognizer)
    : arena_(arena), recognizer_(recognizer) {
  FX_CHECK(arena_ != nullptr);
  FX_CHECK(recognizer_ != nullptr);
}

bool ArenaMember::Accept() {
  if (status_ == Status::kContending) {
    SetWin();
    arena_->TryToResolve();
  }
  return status_ == Status::kWinner;
}

void ArenaMember::Reject() {
  if (status_ == Status::kContending || status_ == Status::kWinner) {
    SetDefeat();
    arena_->TryToResolve();
  }
  is_active_ = false;
}

void ArenaMember::Hold() { is_holding_ = true; }

void ArenaMember::Release() { is_holding_ = false; }

bool ArenaMember::is_holding() const { return is_holding_; }

void ArenaMember::SetWin() {
  FX_DCHECK(status_ == Status::kContending);
  FX_LOGS(INFO) << "winning recognizer: " << recognizer_->DebugName();
  status_ = Status::kWinner;
  recognizer_->OnWin();
}

void ArenaMember::SetDefeat() {
  FX_LOGS(INFO) << "defeated recognizer: " << recognizer_->DebugName();
  status_ = Status::kDefeated;
  recognizer_->OnDefeat();
  Release();  // Does nothing if not holding.
}

void ArenaMember::Reset() {
  status_ = Status::kContending;
  is_active_ = true;
  is_holding_ = false;
}

GestureArena::GestureArena(PointerEventRouter::OnStreamHandledCallback on_stream_handled_callback,
                           EventHandlingPolicy event_handling_policy)
    : router_(std::move(on_stream_handled_callback)),
      event_handling_policy_(event_handling_policy) {}

ArenaMember* GestureArena::Add(GestureRecognizer* recognizer) {
  FX_CHECK(!router_.is_active())
      << "Trying to add a new gesture recognizer to an arena which is already active.";
  arena_members_.push_back(std::make_unique<ArenaMember>(this, recognizer));
  return arena_members_.back().get();
}

void GestureArena::OnEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  FX_CHECK(!arena_members_.empty()) << "The a11y Gesture arena is listening for pointer events "
                                       "but has no added gesture recognizer.";
  if (IsIdle()) {
    // An idle arena received a new event. Starts a new contest.
    StartNewContest();
  }

  router_.RouteEvent(pointer_event, arena_members_);
  TryToResolve();
  switch (state_) {
    case State::kContendingInProgress:
      if (IsIdle()) {
        // The arena has reached the end of an interaction with no winners. Sweep all members and
        // declares the first one to win.
        Sweep();
      }
      return;
    case State::kAssigned:
      if (IsIdle()) {
        // The arena has reached the end of an interaction with a winner.
        HandleEvents(/*consumed_by_member=*/true);
      }
      return;
    case State::kEmpty:
      // The arena has no members left, but still needs to handle incoming events. That depends on
      // the policy in which it was configured. Although an empty arena is handled when it becomes
      // empty, it is also necessary to continue handling events until the interaction is over.
      HandleEvents(/*consumed_by_member=*/false);
      return;
    default:
      return;
  };
}

void GestureArena::TryToResolve() {
  if (state_ == State::kEmpty) {
    return;
  }
  auto [winner, contending] = ClassifyArenaMembers(arena_members_);

  if (state_ == State::kAssigned && !winner) {
    // All members have left the arena, including the winner.
    state_ = State::kEmpty;
    HandleEvents(/*consumed_by_member=*/false);
    return;
  }

  if (state_ == State::kContendingInProgress) {
    if (winner) {
      state_ = State::kAssigned;
      // Someone claimed a win, inform everyone else about their defeat.
      for (auto& member : contending) {
        member->SetDefeat();
      }
    } else if (contending.size() == 1) {
      // When there is no winner and only the last contending is left, it wins.
      state_ = State::kAssigned;
      contending.front()->SetWin();
    }
  }
}

void GestureArena::Reset() {
  FX_CHECK(!router_.is_active()) << "Trying to reset an arena which has cached pointer events";
  state_ = State::kContendingInProgress;
  for (auto& member : arena_members_) {
    member->Reset();
  }
}

bool GestureArena::IsHeld() const {
  for (const auto& member : arena_members_) {
    if (member->status() != ArenaMember::Status::kDefeated && member->is_holding()) {
      return true;
    }
  }
  return false;
}

void GestureArena::StartNewContest() {
  Reset();
  for (auto& member : arena_members_) {
    member->recognizer()->OnContestStarted();
  }
}

void GestureArena::HandleEvents(bool consumed_by_member) {
  if (consumed_by_member || event_handling_policy_ == EventHandlingPolicy::kConsumeEvents) {
    return router_.ConsumePointerEvents();
  }
  FX_DCHECK(event_handling_policy_ == EventHandlingPolicy::kRejectEvents);
  router_.RejectPointerEvents();
}

void GestureArena::Sweep() {
  auto [winner, contending] = ClassifyArenaMembers(arena_members_);
  FX_CHECK(!winner) << "Trying to sweep an arena which has a winner.";
  FX_CHECK(!contending.empty()) << "Trying to sweep an arena with no contending members left.";
  auto it = contending.begin();
  (*it)->SetWin();
  ++it;  // All but the first contender are defeated.
  for (; it != contending.end(); ++it) {
    (*it)->SetDefeat();
  }
}

bool GestureArena::IsIdle() const { return !IsHeld() && !router_.is_active(); }

}  // namespace a11y
