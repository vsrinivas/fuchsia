// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_GESTURE_ARENA_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_GESTURE_ARENA_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>

#include <vector>

#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"

namespace a11y {

class GestureArena;
class PointerEventRouter;

// Represents a recognizer in a Gesture Arena contending for a gesture.
//
// Recognizers add themselves to the arena via GestureArena::Add(), and receive a ArenaMember in
// return. Recognizers are then expected to:
// 1. Call ClaimWin(), when they want to win the arena.
// 2. DeclareDefeat() when they want to leave the arena.
//
// For a group of recognizers in an arena, it is also true:
// 1. Multiple recognizers are kContending -> One becomes kWinner, remainder
// kDefeated.
// 2. Multiple recognizers are kContending -> All but the last declare
// kDefeated, the last is assigned kWinner.
class ArenaMember {
 public:
  enum Status {
    kContending,  // Competing to handle the gesture
    kWinner,      // Won the arena for the gesture
    kDefeated,    // Lost the arena for this gesture.
  };
  ArenaMember(GestureArena* arena, PointerEventRouter* router, GestureRecognizer* recognizer);
  virtual ~ArenaMember() = default;

  // Returns the status of this member in the Arena. Once the gesture has been
  // handled, the arena is responsible for calling Reset() on this object to
  // start a new contending.
  Status status() { return status_; }

  // Returns true if this member should receive pointer events, either because it is still
  // contending or it has won the arena.
  bool IsActive() const { return status_ == kContending || status_ == kWinner; }

  // Prepares the arena member to a new contending.
  void Reset();

  // Claims a win in this arena. If successful, the recognizer then receive a
  // call to OnWin(), to indicate that it has won the arena. If not successful,
  // returns false, indicating that this member is already defeated.
  bool ClaimWin();

  // Leave this arena. The recognizer then receive a call to
  // OnDefeat(), to indicate that it has lost the arena. Returns false if a
  // winner member tries to call DeclareDefeat(), after winning.
  virtual bool DeclareDefeat();

  // Only a member that has won the arena can stop routing pointer events.
  // When it has won the arena and finished processing pointer events, it
  // calls this method to indicate what should be done with pointer events
  // (consume / reject - please see
  // |fuchsia::ui::input::accessibility::EventHandling| for more info). This
  // causes the arena to be restarted and all members will participate again.
  // Normally, the winner recognizer waits to see if its gesture is
  // recognized, and, if it is, calls this method when it has done processing.
  // If, for some reason the winner decides not to consume the pointer events,
  // it calls at any time rejecting the rest of pointer events.
  virtual bool StopRoutingPointerEvents(fuchsia::ui::input::accessibility::EventHandling handled);

  GestureRecognizer* recognizer() { return recognizer_; }

 private:
  GestureArena* const arena_ = nullptr;
  PointerEventRouter* const router_ = nullptr;
  GestureRecognizer* const recognizer_ = nullptr;
  Status status_ = kContending;
  bool stopped_receiving_pointer_events_ = false;
};

// Routes accessibility pointer events to recognizers.
//
// The PointerEventRouter manages the life cycle of accessibility pointer
// events arriving from the OS input system. They are dispatched to
// recognizers that are still contending for the gesture in the arena or to
// the recgonizer that has won the arena. Once the winner recognizer is done
// processing this stream of pointer events, it asks the router to either
// consume or reject the pointer events. Please see
// |fuchsia.ui.input.accessibility.EventHandling| for more info on consuming /
// rejecting pointer events.
class PointerEventRouter {
 public:
  // Callback signature used to indicate when and how an pointer event sent to the arena was
  // processed.
  using OnEventCallback =
      fit::function<void(uint32_t device_id, uint32_t pointer_id,
                         fuchsia::ui::input::accessibility::EventHandling handled)>;

  PointerEventRouter() = default;
  ~PointerEventRouter() = default;

  // Rejects all pointer event streams received by the router and resets the router to receive new
  // ones.
  void RejectPointerEvents();

  // Consumes all pointer event streams received by the router and resets the router to receive new
  // ones.
  void ConsumePointerEvents();

  // Dispatches the pointer event to all active arena members. In this
  // process, also caches the callback from the input system to notify it
  // later whether the pointer events were consumed / rejected.
  void RouteEventToArenaMembers(fuchsia::ui::input::accessibility::PointerEvent pointer_event,
                                OnEventCallback callback,
                                const std::vector<std::unique_ptr<ArenaMember>>& arena_members);

  // Returns true if it has any pending pointer event streams waiting a response whether they were
  // consumed / rejected.
  bool IsActive() { return !pointer_event_callbacks_.empty(); }

 private:
  void InvokePointerEventCallbacks(fuchsia::ui::input::accessibility::EventHandling handled);

  // Holds callbacks associated with pointer event streams in order to notify the input system
  // whether they were consumed / rejected. A pointer event stream is a sequence of pointer events
  // that must start with an ADD phase event and end with an REMOVE phase event. Since only one
  // callback is needed to notify the input system per stream, the ADD event callback is stored.
  // Note: this is a map holding just a few keys and follows the map type selection guidance
  // described at:
  // https://chromium.googlesource.com/chromium/src/+/master/base/containers/README.md#map-and-set-selection
  std::map<std::pair</*pointer_id=*/uint32_t, /*device_id=*/uint32_t>, std::vector<OnEventCallback>>
      pointer_event_callbacks_;
};

// The Gesture Arena for accessibility services.
//
// The Gesture Arena manages several recognizers which are trying to interpret a
// gesture that is being performed. It respects the following rules:
// 1. The first recognizer to claim a win, wins.
// 2. The last recognizer to be in the arena wins.
//
// Any recognizer can declare win or defeat at any time. Once a recognizer has
// won the arena for that gesture, it receives incoming pointer events until it
// tells the arena that it has finished processing pointer events.
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
// and are dispatched to arena members via a PointerEventRouter.
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
// Implementation notes: this arena is heavily influenced by Fluttter's gesture
// arena: https://flutter.dev/docs/development/ui/advanced/gestures
// For those familiar how Flutter version works, here are the important main
// differences:
// 1. It does not implement a sweep logic, which means that all recognizers must
// not be passive -- in another words, they always must at some point call
// ClaimWin() or DeclareDefeat(), as the arena will never force a resolution
// when all pointer IDs have stopped contacting the screen.
// 2. The arena here is not per finger (AKA per pointer ID), which means that
// recognizers receive the whole interaction with the screen.
class GestureArena {
 public:
  GestureArena();
  ~GestureArena() = default;

  // Adds a new recognizer to participate in the arena. The arena returns a
  // pointer to a ArenaMember object, which is the interface where members talk
  // to the arena to declare a win, defeat and so on.
  ArenaMember* Add(GestureRecognizer* recognizer);

  // Returns all arena members.
  const std::vector<std::unique_ptr<ArenaMember>>& arena_members() { return arena_members_; }

  // Dispatches a new pointer event to this arena. This event gets sent to all
  // arena members which are active at the moment.
  void OnEvent(fuchsia::ui::input::accessibility::PointerEvent pointer_event,
               PointerEventRouter::OnEventCallback callback);

  // Tries to resolve the arena if it is not resolved already.
  // It follows two rules:
  // 1. If a recgonizer declared a win, it wins.
  // 2. If a recgonizer is the last one active, it wins.
  // A resolved arena will continue to be so until the winner recognizer for
  // that arena calls ArenaMember::StopRoutingPointerEvents(), which restarts
  // the arena for a new contending.
  void TryToResolve();

  // Resets the arena for a new contending.
  void Reset();

 private:
  PointerEventRouter router_;
  std::vector<std::unique_ptr<ArenaMember>> arena_members_;
  bool resolved_ = false;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_GESTURE_ARENA_H_
