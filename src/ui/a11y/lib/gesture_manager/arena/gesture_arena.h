// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_GESTURE_ARENA_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_GESTURE_ARENA_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>

#include <list>
#include <map>
#include <set>

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

  // Resets the handled status for subsequent pointer event streams.
  void Reset();

  // Rejects all pointer event streams received by the tracker until reset.
  void RejectPointerEvents();

  // Consumes all pointer event streams received by the tracker until reset.
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
  // https://chromium.googlesource.com/chromium/src/+/HEAD/base/containers/README.md#map-and-set-selection
  std::map<StreamID, uint32_t> pointer_event_callbacks_;

  // Holds the streams in progress tracked by the tracker. A stream of pointer events is considered
  // to be active when an event with phase ADD was seen, but not an event with phase REMOVE yet.
  std::set<StreamID> active_streams_;
  std::optional<fuchsia::ui::input::accessibility::EventHandling> handled_;
};

class GestureRecognizer;

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
// Recognizers continue to receive incoming pointer events until they release their |ContestMember|
// or are defeated. After the winning recognizer releases its |ContestMember|, the next interaction
// will begin a new contest.
//
// The order in which recognizers are added to the arena determines event dispatch order and win
// priority. When routing pointer events to recognizers, they see the event in order they were
// added. Then they have the chance to claim a win before the next recognizer in the list has the
// chance to act. Then, during resolution, if multiple recognizers claim a win, the one that was
// added first is awarded the win.
//
// In this model, it is important to notice that there are two layers of abstraction:
// 1. Raw pointer events, which come from the input system, arrive at the arena and are dispatched
//    to recognizers via a PointerStreamTracker.
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
// If any member claims a win, the input system is immediately notified that the pointer event
// streams were consumed (as would be any new pointer event streams until the end of the gesture).
// If no member claims a win, the input system is notified that the pointer event streams were
// rejected.
//
// Implementation notes: this arena is heavily influenced by Fluttter's gesture arena:
// https://flutter.dev/docs/development/ui/advanced/gestures For those familiar how Flutter version
// works, here are the important main differences:
// - The arena here is not per finger (a.k.a. per pointer ID), which means that
//   recognizers receive the whole interaction with the screen.
// - There are not default wins or mutiple levels of acceptance. Recognizers must be certain when
//   they claim a win.
class GestureArena {
 public:
  enum class State {
    kIdle,
    kInProgress,
    kWinnerAssigned,
    kAllDefeated,
    kContestEndedWinnerAssigned,
    kContestEndedAllDefeated,
  };

  // This arena takes |on_stream_handled_callback|, which is called whenever a
  // stream of pointer events is handled (e.g., is consumed or rejected).
  explicit GestureArena(PointerStreamTracker::OnStreamHandledCallback on_stream_handled_callback =
                            [](auto...) {});
  virtual ~GestureArena() = default;

  // Adds a new recognizer to the arena. The new recognizer starts participating in the next
  // contest.
  void Add(GestureRecognizer* recognizer);

  // Dispatches a new pointer event to this arena. This event gets sent to all arena members which
  // are active at the moment.
  //
  // Virtual for testing; overridden by a mock.
  virtual void OnEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

  // Tries to resolve the arena if it is not resolved already.
  // It follows two rules:
  //  * Contests continue until every member has either claimed a win or declared defeat.
  //  * Of the members that claim a win, the win is awarded to the highest priority member.
  // A resolved arena will continue to be so until the winner releases its |ContestMember|, which
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

  // Tracks the state of a recognizer in an arena and backs the state of a |ContestMember| during a
  // contest.
  struct ArenaMember {
    GestureRecognizer* recognizer;
    ContestMember::Status status;
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

  // Informs Scenic of whether input event streams involved in the current contest should be
  // consumed or rejected.
  void HandleEvents(bool consumed_by_member);

  PointerStreamTracker streams_;
  std::list<ArenaMember> arena_members_;
  size_t undecided_members_ = 0;

  fxl::WeakPtrFactory<GestureArena> weak_ptr_factory_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_GESTURE_ARENA_H_
