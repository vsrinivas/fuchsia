// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_MANAGER_V2_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_MANAGER_V2_H_

#include <fuchsia/ui/pointer/augment/cpp/fidl.h>
#include <fuchsia/ui/pointer/cpp/fidl.h>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "src/ui/a11y/lib/gesture_manager/arena_v2/gesture_arena_v2.h"

namespace a11y {

// Talks to `fuchsia.ui.pointer.augment.TouchSourceWithLocalHit.Watch` to
// receive touch events and participate in system-level gesture disambiguation.
//
// Starts calling `Watch` immediately upon construction.
//
// NOTE: this is not instantiated anywhere yet.
class GestureManagerV2 {
 public:
  // Used in unit tests, to inject a fake GestureArena.
  using ArenaFactory =
      fit::function<std::unique_ptr<GestureArenaV2>(InteractionTracker::HeldInteractionCallback)>;

  explicit GestureManagerV2(fuchsia::ui::pointer::augment::TouchSourceWithLocalHitPtr touch_source);

  // Used in unit tests, to inject a fake GestureArena.
  explicit GestureManagerV2(fuchsia::ui::pointer::augment::TouchSourceWithLocalHitPtr touch_source,
                            ArenaFactory arena_factory);

 private:
  // Call `TouchSourceWithLocalHit.Watch` repeatedly, responding to touch events.
  void WatchForTouchEvents(std::vector<fuchsia::ui::pointer::TouchResponse> responses);

  // Send the events to the a11y gesture arena, and generate responses.
  std::vector<fuchsia::ui::pointer::TouchResponse> HandleEvents(
      std::vector<fuchsia::ui::pointer::augment::TouchEventWithLocalHit> events);

  // Send the event to the a11y gesture arena, and generate a response.
  fuchsia::ui::pointer::TouchResponse HandleEvent(
      const fuchsia::ui::pointer::augment::TouchEventWithLocalHit& event);

  // API to get touch events, and participate in system-level gesture disambiguation.
  fuchsia::ui::pointer::augment::TouchSourceWithLocalHitPtr touch_source_;

  // Gesture arena, to perform a11y-specific gesture disambiguation.
  //
  // Whenever an a11y gesture is recognized, we notify the system-level gesture
  // disambiguation that those touch events are ours.
  std::unique_ptr<GestureArenaV2> arena_;

  std::optional<int32_t> touch_device_id_;

  // A rectangle in the same coordinate space as touch event positions. The
  // edges of the rectangle correspond to the edges of the physical screen.
  //
  // All touch events are expected to lie inside this rectangle.
  std::optional<fuchsia::ui::pointer::Rectangle> viewport_bounds_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_MANAGER_V2_H_
