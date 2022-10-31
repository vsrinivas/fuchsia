// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_V2_GESTURE_ARENA_V2_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_V2_GESTURE_ARENA_V2_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <fuchsia/ui/pointer/augment/cpp/fidl.h>

#include <list>
#include <map>
#include <set>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/a11y/lib/gesture_manager/arena_v2/participation_token_interface.h"

namespace a11y {

// Return value for |GestureArenaV2::OnEvent|, indicating the status of the current contest.
enum class ContestStatus {
  kUnresolved,
  kWinnerAssigned,
  kAllLosers,
};

// |InteractionTracker| tracks the life cycle of interactions arriving from Scenic.
//
// Every interaction is eventually either consumed or rejected, at which point
// |OnInteractionHandledCallback| is called with the appropriate arguments.
class InteractionTracker {
 public:
  // Callback signature used to indicate to Scenic how an interaction sent to the
  // a11y gesture arena was handled.
  using OnInteractionHandledCallback =
      fit::function<void(uint32_t device_id, uint32_t pointer_id, ContestStatus status)>;

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
  // For ADD events, this records some state to remember to notify the input system
  // when the interaction is consumed or rejected.
  void OnEvent(const fuchsia::ui::pointer::augment::TouchEventWithLocalHit& event);

  // Returns true if there are any open interactions.
  //
  // An interaction is considered closed when there is an event with phase == REMOVE.
  bool is_active() const { return !open_interactions_.empty(); }

  // What is the current status of the interaction tracker?
  //
  // The tracker can be in "accept mode", "reject mode", or currently "undecided", represented
  // by the three variants of |ContestStatus|.
  ContestStatus Status() { return status_; }

 private:
  // Used to identify an interaction. A pair is used rather
  // than a structure for a comparable key for std::set.
  using InteractionID = std::pair</*device_id=*/uint32_t, /*pointer_id=*/uint32_t>;

  // Handle all open interactions, and enter a state where all future interactions
  // will be handled in the same way.
  void InvokePointerEventCallbacks(ContestStatus status);

  // Callback used to notify how each interaction was handled.
  //
  // This gets called once per interaction.
  OnInteractionHandledCallback on_interaction_handled_callback_;

  // Holds how many times the |on_interaction_handled_callback_| should be invoked,
  // in order to notify the input system that the interactions were consumed / rejected.
  //
  // An interaction is a sequence of pointer events that must start with an ADD
  // phase event and end with an REMOVE phase event. Since only one callback
  // call is needed to notify the input system per interaction, on an ADD event
  // the count is increased.
  //
  // Note: this is a map holding just a few keys and follows the map type selection guidance
  // described at:
  // https://chromium.googlesource.com/chromium/src/+/HEAD/base/containers/README.md#map-and-set-selection
  std::map<InteractionID, uint32_t> interaction_callbacks_;

  // Holds the currently open interactions. An interaction is considered open if an event
  // with phase ADD was seen, but not an event with phase REMOVE yet.
  std::set<InteractionID> open_interactions_;

  // Whether the tracker is in "accept mode", "reject mode", or currently "undecided".
  //
  // Gets set when a user calls `ConsumePointerEvents`, or `RejectPointerEvents`, or
  // `Reset`.
  ContestStatus status_;
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
// contest. (Or, if all recognziers reject, then the next interaction will begin
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
  // This arena takes |on_interaction_handled_callback|, which is called whenever an
  // interaction is handled (e.g., is consumed or rejected).
  explicit GestureArenaV2(
      InteractionTracker::OnInteractionHandledCallback on_interaction_handled_callback =
          [](auto...) {});
  virtual ~GestureArenaV2() = default;

  // Adds a new recognizer to the arena. The new recognizer starts participating in the next
  // contest.
  void Add(GestureRecognizerV2* recognizer);

  // Dispatches a new pointer event to this arena. This event gets sent to all participating
  // recognizers.
  //
  // Virtual for testing; overridden by a mock.
  virtual ContestStatus OnEvent(const fuchsia::ui::pointer::augment::TouchEventWithLocalHit& event);

  // Tries to resolve the arena if it is not resolved already.
  //
  // It follows two rules:
  //  * Contests remain unresolved until every recognizer has accepted or rejected.
  //  * Of the recognizers that accept, the win is awarded to one with highest priority.
  // A resolved arena stays resolved (and the current contest continues) until the winner
  // releases its |ParticipationToken|, which resets the arena for a new contest.
  void TryToResolve();

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

  // Informs Scenic of whether interactions involved in the current contest should be
  // consumed or rejected.
  void HandleEvents(bool consumed);

  InteractionTracker interactions_;

  std::list<RecognizerHandle> recognizers_;

  size_t undecided_recognizers_ = 0;

  fxl::WeakPtrFactory<GestureArenaV2> weak_ptr_factory_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_V2_GESTURE_ARENA_V2_H_
