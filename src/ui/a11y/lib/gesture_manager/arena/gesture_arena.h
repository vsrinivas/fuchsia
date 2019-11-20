// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_GESTURE_ARENA_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_GESTURE_ARENA_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>

#include <map>
#include <set>
#include <vector>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/a11y/lib/gesture_manager/arena/contest_member.h"

namespace a11y {

// The PointerStreamTracker tracks the life cycle of accessibility pointer streams arriving from the
// OS input system. It can consume or reject tracked pointer streams. Please see
// fuchsia.ui.input.accessibility.EventHandling| for more info on consuming / rejecting pointer
// events.
class PointerStreamTracker {
 public:
  // Callback signature used to indicate when and how an pointer event sent to the arena was
  // processed.
  using OnStreamHandledCallback =
      fit::function<void(uint32_t device_id, uint32_t pointer_id,
                         fuchsia::ui::input::accessibility::EventHandling handled)>;

  explicit PointerStreamTracker(OnStreamHandledCallback on_stream_handled_callback);
  ~PointerStreamTracker() = default;

  // Rejects all pointer event streams received by the tracker, causing it to become inactive.
  void RejectPointerEvents();

  // Consumes all pointer event streams received by the tracker. This does not
  // cause the tracker to become inactive, as it will continue to receive the
  // pointer events for each consumed stream until it finishes.
  void ConsumePointerEvents();

  // Adds or removes a pointer stream for the given event. For ADD events, also caches the callback
  // from the input system to notify it later whether the stream was consumed or rejected.
  void OnEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

  // Returns true if it has any ongoing pointer event streams which are not
  // finished yet. A stream is considered finished when it sees and event with
  // phase == REMOVE.
  bool is_active() const { return !active_streams_.empty(); }

 private:
  // Used to uniquely identify a pointer event stream. A pair is used rather
  // than a structure for a comparable key for std::set.
  using StreamID = std::pair</*device_id=*/uint32_t, /*pointer_id=*/uint32_t>;
  void InvokePointerEventCallbacks(fuchsia::ui::input::accessibility::EventHandling handled);

  // Callback used to notify how each stream was handled.
  OnStreamHandledCallback on_stream_handled_callback_;

  // Holds how many times the |on_stream_handled_callback_| should be invoked per pointer event
  // streams in order to notify the input system whether they were consumed / rejected. A pointer
  // event stream is a sequence of pointer events that must start with an ADD phase event and end
  // with an REMOVE phase event. Since only one callback call is needed to notify the input system
  // per stream, on an ADD event the count is increased.
  //
  // Note: this is a map holding just a few keys and follows the map type selection guidance
  // described at:
  // https://chromium.googlesource.com/chromium/src/+/master/base/containers/README.md#map-and-set-selection
  std::map<StreamID, uint32_t> pointer_event_callbacks_;

  // Holds the streams in progress tracked by the tracker. A stream of pointer events is considered
  // to be active when an event with phase ADD was seen, but not an event with phase REMOVE yet.
  std::set<StreamID> active_streams_;
};

class GestureRecognizer;

// The Gesture Arena for accessibility services.
//
// The Gesture Arena manages several recognizers which are trying to interpret a
// gesture that is being performed. It respects the following rules:
// 1. The first recognizer to claim a win, wins.
// 2. The last recognizer to be in the arena wins.
// 3. If the arena is not held and no recognizer won by the end of an
// interaction, sweeps the arena and turn the first recognizer the winner.
//
// Any recognizer can declare win or defeat at any time. Once a recognizer has
// won the arena for that gesture, it receives incoming pointer events until it releases its
// |ContestMember|.
//
// The order in which recognizers are added to the arena is important. When
// routing pointer events to recognizers, they see the event in order they were
// added. Then they have the chance to claim a win before the next recognizer in
// the list has the chance to act.
// If a recognizer claims a win and is successful, this means that all other
// recognizers will be defeated and will be notified via a OnDefeat() call.
//
// In this model, it is important to notice that there are two layers of
// abstraction:
// 1. Raw pointer events, which come from the input system, arrive at the arena
// and are dispatched to arena members via a PointerStreamTracker.
// 2. Gestures, which are sequences of pointer events with a semantic meaning,
// are identified by recognizers.
//
// With that in mind, each recognizer defines the semantic meaning for the
// sequence of pointer events that it is receiving. In another words, it is
// expected that a recognizer could identify a single tap, another a double tap,
// and so on.
//
// For this reason, winning the gesture arena **does not mean** that the
// recognizer identified a gesture. It only means that it has the right to
// interpret the raw pointer events, and give a semantic meaning to them if it
// wants to.
// Consider an arena with only one recognizer, which, by the rules above, would
// be a winner by the default. It would process incoming pointer events and
// detect its gesture only when the gesture actually occurred.
//
// Recognizers should not destroy the arena.
//
// Implementation notes: this arena is heavily influenced by Fluttter's gesture
// arena: https://flutter.dev/docs/development/ui/advanced/gestures
// For those familiar how Flutter version works, here are the important main
// differences:
// - The arena here is not per finger (AKA per pointer ID), which means that
// recognizers receive the whole interaction with the screen.
class GestureArena {
 public:
  enum class State {
    kContendingInProgress,  // One or more recognizers are still contending.
    kAssigned,              // A winner has been assigned and processes events.
    kEmpty,                 // All recognizers have left the arena.
  };

  enum class EventHandlingPolicy {
    kConsumeEvents,  // All events will be consumed by this arena even when it becomes empty.
    kRejectEvents,   // All events will be rejected by this arena when it becomes empty.
  };

  // This arena takes |on_stream_handled_callback|, which is called whenever a
  // stream of pointer events is handled (e.g., is consumed or rejected).
  explicit GestureArena(
      PointerStreamTracker::OnStreamHandledCallback on_stream_handled_callback = [](auto...) {},
      EventHandlingPolicy event_handling_policy = EventHandlingPolicy::kRejectEvents);
  ~GestureArena() = default;

  // Adds a new recognizer to participate in the arena.
  void Add(GestureRecognizer* recognizer);

  // Changes the event handling policy for empty arenas.
  void event_handling_policy(EventHandlingPolicy value) { event_handling_policy_ = value; }

  // Dispatches a new pointer event to this arena. This event gets sent to all arena members which
  // are active at the moment.
  void OnEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

  // Tries to resolve the arena if it is not resolved already.
  // It follows two rules:
  // 1. If a recognizer declared a win, it wins.
  // 2. If a recognizer is the last one active, it wins.
  // A resolved arena will continue to be so until the interaction is over and
  // the arena is not held, which restarts the arena for a new contest.
  void TryToResolve();

  // Resets the arena for a new contest.
  void Reset();

  // Returns true if there is any non-defeated arena member holding the arena.
  bool IsHeld() const;

 private:
  class ArenaContestMember;

  // Tracks the state of a recognizer in an arena and backs the state of a |ContestMmeber| during a
  // contest.
  struct ArenaMember {
    GestureRecognizer* recognizer;
    ContestMember::Status status;

    void SetWin();
    void SetDefeat();
  };

  // Holds pointers to arena members in different states.
  // If winner is not nullptr, contending is empty. If contending is not empty, winner is nullptr.
  struct ClassifiedArenaMembers {
    ArenaMember* winner = nullptr;
    std::vector<ArenaMember*> contending;
  };

  // Dispatches the pointer event to active arena members and purges inactive contest members.
  void DispatchEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

  // Classifies the arena members into winner and contending.
  // Example:
  // auto [winner, contending] = ClassifyArenaMembers();
  ClassifiedArenaMembers ClassifyArenaMembers();

  // Returns true if the arena is not held and the interaction is finished.
  bool IsIdle() const;

  // Resets the arena and notify members that a new contest has started.
  void StartNewContest();

  // Sweeps the arena granting the win to the first recognizer that is contending.
  void Sweep();

  // Handles how accessibility pointer events will be used by this arena. This depends on the policy
  // in which it was configured during its construction.
  void HandleEvents(bool consumed_by_member);

  PointerStreamTracker streams_;
  std::vector<ArenaMember> arena_members_;
  std::vector<fxl::WeakPtr<ArenaContestMember>> contest_members_;
  State state_ = State::kContendingInProgress;
  EventHandlingPolicy event_handling_policy_ = EventHandlingPolicy::kConsumeEvents;

  fxl::WeakPtrFactory<GestureArena> weak_ptr_factory_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_GESTURE_ARENA_H_
