// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_V2_GESTURE_ARENA_V2_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_V2_GESTURE_ARENA_V2_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <fuchsia/ui/pointer/augment/cpp/fidl.h>
#include <fuchsia/ui/pointer/cpp/fidl.h>

#include <cstdint>
#include <list>
#include <map>
#include <set>
#include <tuple>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/a11y/lib/gesture_manager/arena_v2/participation_token_interface.h"

namespace a11y {

// Helper class for |GestureArenaV2|.
//
// Tracks the "decision status" of the current contest -- meaning whether or not
// the contest will eventually result in a unique winner, or in all losers.
// * As soon as at least one recognizer tries to claim a win, the status is
//   "accept".
// * If all recognizers decide to reject, then the status is "reject".
// (Note the subtle difference between this and the notion of a contest being
// completely "resolved"!)
//
// This also tracks "open" interactions.  An interaction is considered open if
// it has no `REMOVE` or `CANCEL` event yet; otherwise we say it is closed.
//
// Lastly, this tracks interactions that are "on hold". We say that an
// interaction is on hold if the current contest was undecided when that
// interaction became closed. In this case, the interaction stays on hold until
// the current contest is decided, at which point we fire a callback for that
// interaction, and it is longer on hold.
class InteractionTracker {
 public:
  // The "decision status" of the current contest. If it's certain there will
  // eventually be a winner, the status is "accept". If all recognizers dropped
  // out, the status is "reject".
  enum class ConsumptionStatus {
    kUndecided,
    kAccept,
    kReject,
  };

  // Callback fired once per interaction that was "on hold", once
  // the current contest's consumption status is decided.
  //
  // `status` will never be |ConsumptionStatus::kUndecided|.
  using HeldInteractionCallback =
      fit::function<void(fuchsia::ui::pointer::TouchInteractionId id, ConsumptionStatus status)>;

  explicit InteractionTracker(HeldInteractionCallback callback);

  // Resets the current contest's consumption status; should be called after a
  // contest ends.
  void Reset();

  // Set the consumption status to "accept", and notify all interactions that
  // were on hold.
  void AcceptInteractions();

  // Set the consumption status to "reject", and notify all interactions that
  // were on hold.
  void RejectInteractions();

  // Handle a new touch event, keeping track of which interactions are "open" or
  // "on hold".
  void OnEvent(const fuchsia::ui::pointer::augment::TouchEventWithLocalHit& event);

  // What is the consumption status of the current contest?
  ConsumptionStatus Status();

  // Are there any open interactions?
  bool HasOpenInteractions() const;

 private:
  // Notify all interactions that were on hold, telling them that the
  // consumption status of the current contest has been decided.
  void NotifyHeldInteractions();

  // Callback fired once per interaction that was "on hold".
  HeldInteractionCallback callback_;

  // The consumption status of the current contest.
  ConsumptionStatus status_ = ConsumptionStatus::kUndecided;

  // The set of currently open interactions.
  std::set<std::tuple<uint32_t, uint32_t, uint32_t>> open_interactions_;

  // The set of interactions that are currently on hold.
  std::vector<fuchsia::ui::pointer::TouchInteractionId> held_interactions_;
};

class GestureRecognizerV2;

// The Gesture Arena for accessibility services.
//
// The Gesture Arena manages several recognizers which are trying to interpret a gesture that is
// being performed. It respects the following rules:
//  * Contests begin at the start of an interaction, and continue until every recognizer has either
//    accepted or rejected.
//  * Of the recognizers that accept, a win is awarded to the highest priority recognizer.
//
// All recognizers must eventually accept or reject. Once a recognizer has decided, it may not
// change its mind.
//
// Recognizers continue to receive incoming pointer events until they release
// their |ParticipationToken| or are defeated. After the winning recognizer
// releases its |ParticipationToken|, the next interaction will begin a new
// contest. (Or, if all recognizers reject, then the next interaction will begin
// a new contest.)
//
// The order in which recognizers are added to the arena determines event dispatch order and win
// priority. When routing pointer events to recognizers, they see the event in order they were
// added. During resolution, if multiple recognizers declared "accept", the one that was added
// first is awarded the win.
//
// In this model, it is important to notice that there are two layers of abstraction:
// 1. Raw pointer events, which come from the input system, arrive at the arena and are dispatched
//    to recognizers via an |InteractionTracker|.
// 2. Gestures, which are sequences of pointer events with a semantic meaning, are identified by
//    recognizers.
//
// With that in mind, each recognizer defines the semantic meaning for the sequence of pointer
// events that it is receiving. In another words, it is expected that a recognizer could identify a
// single tap, another a double tap, and so on.
//
// "Accepting" indicates that a recognizer identified a gesture. However, that recognizer may not
// necessarily be awarded a win, if a higher priority recognizer also accepts. Recognizers are free
// to handle their events optimistically, but if they do then they must undo/reset any changes they
// effect if they are eventually defeated. For example, a single-tap recognizer may accept, but if
// a higher-priority double-tap recognzier also accepts, the latter will win.
//
// Recognizers should not destroy the arena.
//
// If any recognizer accepts, the input system is immediately notified that the interactions
// were consumed (as would be any new interactions until the end of the gesture).
// If all recognizers reject, the input system is notified that the interactions were rejected.
//
// Implementation notes: this arena is heavily influenced by Fluttter's gesture arena:
// https://flutter.dev/docs/development/ui/advanced/gestures For those familiar how Flutter version
// works, here are the important main differences:
// - The arena here is not per finger (a.k.a. per pointer ID), which means that recognizers may
//   receive events from multiple interactions (i.e. fingers) concurrently.
// - There are not default wins or multiple levels of acceptance. Recognizers must be certain when
//   they decide to accept.
class GestureArenaV2 {
 public:
  // The arena takes a callback, which is called on each interaction that became
  // closed before the contest's consumption status (accept or reject) was decided.
  //
  // See `InteractionTracker` for details.
  explicit GestureArenaV2(InteractionTracker::HeldInteractionCallback callback = [](auto...) {});
  virtual ~GestureArenaV2() = default;

  // Adds a new recognizer to the arena. The new recognizer starts participating in the next
  // contest.
  void Add(GestureRecognizerV2* recognizer);

  // Dispatches a new pointer event to this arena. This event gets sent to all participating
  // recognizers.
  //
  // Virtual for testing; overridden by a mock.
  virtual InteractionTracker::ConsumptionStatus OnEvent(
      const fuchsia::ui::pointer::augment::TouchEventWithLocalHit& event);

  // Return the consumption status of the current contest.
  InteractionTracker::ConsumptionStatus Status() { return interactions_.Status(); }

 private:
  class ParticipationToken;

  // The status of a recognizer in the current contest.
  //
  // Once a recognizer decides to accept or reject, it cannot change its mind.
  enum class RecognizerStatus {
    kUndecided,
    kAccepted,
    kRejected,
  };

  // Tracks the state of a recognizer in the arena.
  struct RecognizerHandle {
    GestureRecognizerV2* recognizer;

    // Has this recognizer decided to accept or reject the current gesture?
    RecognizerStatus status;

    // If the recognizer is still participating in the current contest, this
    // will point to a token. Otherwise, this will be an invalid weak pointer.
    //
    // Here, "participating" means the recognizer is still undecided, or it
    // has won but it wants to keep receiving events (e.g. a tap-and-hold).
    fxl::WeakPtr<ParticipationToken> participation_token;
  };

  // Dispatches the pointer event to participating recognizers.
  void DispatchEvent(const fuchsia::ui::pointer::augment::TouchEventWithLocalHit& event);

  // Returns whether there are any participating recognizers.
  bool IsHeld() const;

  // Returns true if the arena is not held and there are no open interactions.
  //
  // This is the condition for beginning a new contest.
  bool IsIdle() const;

  // Reset the arena and notify recognizers that a new contest has started.
  void StartNewContest();

  // Try to resolve the current contest.
  //
  // It follows two rules:
  //  * Contests remain unresolved until every recognizer has accepted or rejected.
  //  * Of the recognizers that accept, the win is awarded to one with highest priority.
  //
  // A resolved arena stays resolved (and the current contest continues) until the winner
  // releases its |ParticipationToken| and all open interactions are closed. (Or, if all
  // recognizers declared defeat, simply once all open interactions are closed.)
  void TryToResolve();

  InteractionTracker interactions_;

  std::list<RecognizerHandle> recognizers_;

  size_t undecided_recognizers_ = 0;

  fxl::WeakPtrFactory<GestureArenaV2> weak_ptr_factory_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_V2_GESTURE_ARENA_V2_H_
