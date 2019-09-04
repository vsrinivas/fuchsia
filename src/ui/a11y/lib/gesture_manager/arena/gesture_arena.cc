// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/arena/gesture_arena.h"

#include <lib/syslog/cpp/logger.h>

namespace a11y {

namespace {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;

}  // namespace

void PointerEventRouter::RejectPointerEvents() {
  InvokePointerEventCallbacks(fuchsia::ui::input::accessibility::EventHandling::REJECTED);
}

void PointerEventRouter::ConsumePointerEvents() {
  InvokePointerEventCallbacks(fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
}

void PointerEventRouter::InvokePointerEventCallbacks(
    fuchsia::ui::input::accessibility::EventHandling handled) {
  for (const auto& kv : pointer_event_callbacks_) {
    uint32_t pointer_id = kv.first.first;
    uint32_t device_id = kv.first.second;
    for (const auto& callback : kv.second) {
      callback(pointer_id, device_id, handled);
    }
  }
  pointer_event_callbacks_.clear();
}

void PointerEventRouter::RouteEventToArenaMembers(
    AccessibilityPointerEvent pointer_event,
    fuchsia::ui::input::accessibility::PointerEventListener::OnEventCallback callback,
    const std::vector<std::unique_ptr<ArenaMember>>& arena_members) {
  // Note that at some point we must answer whether the pointer event stream was
  // consumed / rejected. For this reason, for each ADD event we store the
  // callback that will be responsible for signaling how the events were
  // handled. This happens in the future, once the gesture recognizer that wins
  // the arena finishes handling the gesture, it decides what to do. It is
  // important also to mention that for now, is all or nothing: consume or
  // reject all pointer events that were sent to the arena, and not per a
  // pointer event ID basis. Although this can be implemented, there is no use
  // case for this right now.
  if (pointer_event.phase() == Phase::ADD) {
    uint32_t pointer_id = pointer_event.pointer_id();
    uint32_t device_id = pointer_event.device_id();
    pointer_event_callbacks_[{pointer_id, device_id}].push_back(std::move(callback));
  }
  for (const auto& member : arena_members) {
    if (member->IsActive()) {
      member->recognizer()->HandleEvent(pointer_event);
    }
  }
}

ArenaMember::ArenaMember(GestureArena* arena, PointerEventRouter* router,
                         GestureRecognizer* recognizer)
    : arena_(arena), router_(router), recognizer_(recognizer) {
  FX_CHECK(arena_ != nullptr);
  FX_CHECK(router_ != nullptr);
  FX_CHECK(recognizer_ != nullptr);
}

bool ArenaMember::ClaimWin() {
  if (status_ == kDefeated) {
    return false;
  }
  FX_CHECK(status_ != kWinner) << "Declaring the recognizer " << recognizer_->DebugName()
                               << " as winner when it has already won";
  FX_LOGS(INFO) << "winning gesture: " << recognizer_->DebugName();
  status_ = kWinner;
  recognizer_->OnWin();
  arena_->TryToResolve();
  return true;
}

bool ArenaMember::DeclareDefeat() {
  if (status_ == kWinner) {
    return false;
  }
  FX_CHECK(status_ != kDefeated) << "Declaring the recognizer " << recognizer_->DebugName()
                                 << " as defeated when it has already lost";
  FX_LOGS(INFO) << "defeating recognizer " << recognizer_->DebugName();
  status_ = kDefeated;
  stopped_receiving_pointer_events_ = true;
  recognizer_->OnDefeat();
  arena_->TryToResolve();
  return true;
}

void ArenaMember::Reset() {
  FX_CHECK(stopped_receiving_pointer_events_)
      << "The recognizer " << recognizer_->DebugName()
      << " was reset without stopping tracking of pointer events.";
  status_ = kContending;
  stopped_receiving_pointer_events_ = false;
}

bool ArenaMember::StopRoutingPointerEvents(
    fuchsia::ui::input::accessibility::EventHandling handled) {
  if (status_ != kWinner) {
    // Only the winner of the arena can decide where to dispatch the pointer events.
    return false;
  }
  stopped_receiving_pointer_events_ = true;
  switch (handled) {
    case fuchsia::ui::input::accessibility::EventHandling::CONSUMED:
      router_->ConsumePointerEvents();
      break;
    case fuchsia::ui::input::accessibility::EventHandling::REJECTED:
      router_->RejectPointerEvents();
      break;
  };
  arena_->Reset();
  return true;
}

GestureArena::GestureArena() = default;

ArenaMember* GestureArena::Add(GestureRecognizer* recognizer) {
  FX_CHECK(!router_.IsActive())
      << "Trying to add a new gesture recognizer to an arena which is already active.";
  arena_members_.push_back(std::make_unique<ArenaMember>(this, &router_, recognizer));
  const auto& last = arena_members_.back();
  return last.get();
}

void GestureArena::OnEvent(
    fuchsia::ui::input::accessibility::PointerEvent pointer_event,
    fuchsia::ui::input::accessibility::PointerEventListener::OnEventCallback callback) {
  FX_CHECK(!arena_members_.empty()) << "The a11y Gesture arena is listening for pointer events "
                                       "but has no added gesture recognizer.";

  router_.RouteEventToArenaMembers(std::move(pointer_event), std::move(callback), arena_members_);
  TryToResolve();
}

void GestureArena::TryToResolve() {
  if (resolved_) {
    return;
  }
  ArenaMember* winner = nullptr;
  std::vector<ArenaMember*> contending;
  for (auto& member : arena_members_) {
    switch (member->status()) {
      case ArenaMember::kWinner:
        FX_CHECK(!winner) << "A gesture arena can have up to one winner only.";
        winner = member.get();
        break;
      case ArenaMember::kDefeated:
        // No work to do.
        break;
      case ArenaMember::kContending:
        contending.push_back(member.get());
        break;
    };
  }

  if (winner) {
    resolved_ = true;
    // Someone claimed a win, inform everyone else about their defeat.
    for (auto& member : contending) {
      member->DeclareDefeat();
    }
  } else if (contending.size() == 1) {
    // When there is no winner and only the last contending is left, it wins.
    resolved_ = true;
    FX_CHECK(contending.front()->ClaimWin());
  }
}

void GestureArena::Reset() {
  FX_CHECK(!router_.IsActive()) << "Trying to reset an arena which has cached pointer events";
  resolved_ = false;
  for (auto& member : arena_members_) {
    member->Reset();
  }
}

}  // namespace a11y
