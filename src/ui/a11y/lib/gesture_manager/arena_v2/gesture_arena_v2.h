// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_V2_GESTURE_ARENA_V2_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_V2_GESTURE_ARENA_V2_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>

#include <list>
#include <map>
#include <set>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/a11y/lib/gesture_manager/arena_v2/contest_member_v2.h"

namespace a11y {

// |InteractionTracker| tracks the life cycle of interactions arriving from Scenic.
// It can consume or reject interactions.
class InteractionTracker {
 public:
  // Callback signature used to indicate how an interaction sent to the arena was handled.
  using OnInteractionHandledCallback =
      fit::function<void(uint32_t device_id, uint32_t pointer_id,
                         fuchsia::ui::input::accessibility::EventHandling handled)>;

  explicit InteractionTracker(OnInteractionHandledCallback on_interaction_handled_callback);
  ~InteractionTracker() = default;

  // Resets the handled status for subsequent interactions.
  void Reset();

  // Rejects all interactions received by the tracker until reset.
  void RejectPointerEvents();

  // Consumes all interactions received by the tracker until reset.
  void ConsumePointerEvents();

  // Process the given event, which may add or remove an interaction.
  //
  // For ADD events, also caches the callback from the input system to notify it
  // later whether the interaction was consumed or rejected.
  void OnEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

  // Returns true if there are any open interactions.
  // An interaction is considered closed when there is an event with phase == REMOVE.
  bool is_active() const { return !open_interactions_.empty(); }

 private:
  // Used to identify an interaction. A pair is used rather
  // than a structure for a comparable key for std::set.
  using InteractionID = std::pair</*device_id=*/uint32_t, /*pointer_id=*/uint32_t>;

  // Handle all open interactions, and enter a state where all future interactions
  // will be handled in the same way.
  void InvokePointerEventCallbacks(fuchsia::ui::input::accessibility::EventHandling handled);

  // Callback used to notify how each interaction was handled.
  //
  // Note that this gets called once per event in the interaction, not only once per
  // interaction.
  OnInteractionHandledCallback on_interaction_handled_callback_;

  // Holds how many times the |on_interaction_handled_callback_| should be invoked per interaction,
  // in order to notify the input system whether they were consumed / rejected. An interaction
  // is a sequence of pointer events that must start with an ADD phase event and end
  // with an REMOVE phase event. Since only one callback call is needed to notify the input system
  // per interaction, on an ADD event the count is increased.
  //
  // Note: this is a map holding just a few keys and follows the map type selection guidance
  // described at:
  // https://chromium.googlesource.com/chromium/src/+/HEAD/base/containers/README.md#map-and-set-selection
  std::map<InteractionID, uint32_t> pointer_event_callbacks_;

  // Holds the currently open interactions. An interaction is considered open if an event
  // with phase ADD was seen, but not an event with phase REMOVE yet.
  std::set<InteractionID> open_interactions_;

  // Whether the tracker is in "accept mode", "reject mode", or currently "undecided".
  //
  // Gets set when a user calls `ConsumePointerEvents` or `RejectPointerEvents`, and
  // gets reset when a user calls `Reset`.
  std::optional<fuchsia::ui::input::accessibility::EventHandling> handled_;
};

class GestureRecognizerV2;

// The Gesture Arena for accessibility services.
//
// The Gesture Arena manages several recognizers which are trying to interpret a gesture that is
// being performed. It respects the following rules:
//  * Contests begin when a touch pointer is added and continue until every member has either
//    claimed a win or declared defeat.
//  * Of the members that claim a win, the win is awarded to the highest priority member.
//
// All members must eventually claim a win or declare defeat. Once a member has claimed a win or
// declared defeat, it may not change its declaration.
//
// Recognizers continue to receive incoming pointer events until they release their
// |ContestMemberV2| or are defeated. After the winning recognizer releases its |ContestMemberV2|,
// the next interaction will begin a new contest.
//
// The order in which recognizers are added to the arena determines event dispatch order and win
// priority. When routing pointer events to recognizers, they see the event in order they were
// added. Then they have the chance to claim a win before the next recognizer in the list has the
// chance to act. Then, during resolution, if multiple recognizers claim a win, the one that was
// added first is awarded the win.
//
// In this model, it is important to notice that there are two layers of abstraction:
// 1. Raw pointer events, which come from the input system, arrive at the arena and are dispatched
//    to recognizers via an InteractionTracker.
// 2. Gestures, which are sequences of pointer events with a semantic meaning, are identified by
//    recognizers.
//
// With that in mind, each recognizer defines the semantic meaning for the sequence of pointer
// events that it is receiving. In another words, it is expected that a recognizer could identify a
// single tap, another a double tap, and so on.
//
// Claiming a win indicates that a recognizer identified a gesture. However, the win will not
// necessarily be awarded to that recognizer. Recognizers are free to handle their events
// optimistically, but if they do then they must undo/reset any changes they effect if they are
// eventually defeated.
//
// Recognizers should not destroy the arena.
//
// If any member claims a win, the input system is immediately notified that the interactions
// were consumed (as would be any new interactions until the end of the gesture).
// If no member claims a win, the input system is notified that the interactions were rejected.
//
// Implementation notes: this arena is heavily influenced by Fluttter's gesture arena:
// https://flutter.dev/docs/development/ui/advanced/gestures For those familiar how Flutter version
// works, here are the important main differences:
// - The arena here is not per finger (a.k.a. per pointer ID), which means that
//   recognizers receive the whole interaction with the screen.
// - There are not default wins or multiple levels of acceptance. Recognizers must be certain when
//   they claim a win.
class GestureArenaV2 {
 public:
  enum class State {
    kIdle,
    kInProgress,
    kWinnerAssigned,
    kAllDefeated,
    kContestEndedWinnerAssigned,
    kContestEndedAllDefeated,
  };

  // This arena takes |on_interaction_handled_callback|, which is called whenever an
  // interaction is handled (e.g., is consumed or rejected).
  explicit GestureArenaV2(
      InteractionTracker::OnInteractionHandledCallback on_interaction_handled_callback =
          [](auto...) {});
  virtual ~GestureArenaV2() = default;

  // Adds a new recognizer to the arena. The new recognizer starts participating in the next
  // contest.
  void Add(GestureRecognizerV2* recognizer);

  // Dispatches a new pointer event to this arena. This event gets sent to all arena members which
  // are active at the moment.
  //
  // Virtual for testing; overridden by a mock.
  virtual void OnEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

  // Tries to resolve the arena if it is not resolved already.
  // It follows two rules:
  //  * Contests continue until every member has either claimed a win or declared defeat.
  //  * Of the members that claim a win, the win is awarded to the highest priority member.
  // A resolved arena will continue to be so until the winner releases its |ContestMemberV2|, which
  // resets the arena for a new contest.
  void TryToResolve();

  // Get the state of the gesture arena.
  //
  // TODO(fxbug.dev/109939): gesture arena provides its state.
  //
  // Virtual for testing; overridden by a mock.
  virtual State GetState();

 private:
  class ArenaContestMember;

  // Tracks the state of a recognizer in an arena and backs the state of a |ContestMemberV2| during
  // a contest.
  struct ArenaMember {
    GestureRecognizerV2* recognizer;
    ContestMemberV2::Status status;
    fxl::WeakPtr<ArenaContestMember> contest_member;
  };

  // Dispatches the pointer event to active arena members and purges inactive contest members.
  void DispatchEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

  // Returns whether there are any contest members wanting events.
  bool IsHeld() const;

  // Returns true if the arena is not held and the interaction is finished.
  bool IsIdle() const;

  // Resets the arena and notify members that a new contest has started.
  void StartNewContest();

  // Informs Scenic of whether interactions involved in the current contest should be
  // consumed or rejected.
  void HandleEvents(bool consumed_by_member);

  InteractionTracker interactions_;
  std::list<ArenaMember> arena_members_;
  size_t undecided_members_ = 0;

  fxl::WeakPtrFactory<GestureArenaV2> weak_ptr_factory_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_V2_GESTURE_ARENA_V2_H_
