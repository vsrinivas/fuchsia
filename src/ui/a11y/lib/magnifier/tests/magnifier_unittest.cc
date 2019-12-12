// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/magnifier/magnifier.h"

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/zx/time.h>

#include <limits>
#include <optional>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/a11y/lib/gesture_manager/arena/gesture_arena.h"
#include "src/ui/a11y/lib/gesture_manager/arena/tests/mocks/mock_contest_member.h"
#include "src/ui/a11y/lib/magnifier/tests/mocks/mock_magnification_handler.h"
#include "src/ui/a11y/lib/testing/formatting.h"
#include "src/ui/a11y/lib/testing/input.h"
#include "src/ui/lib/glm_workaround/glm_workaround.h"

#include <glm/gtc/epsilon.hpp>

// These tests cover magnifier behavior, mostly around magnifier gestures. Care needs to be taken
// wrt. the constants in magnifier.h. In particular, mind the default, min, and max zoom, and the
// drag detection threshold.

namespace accessibility_test {
namespace {

using fuchsia::ui::input::accessibility::EventHandling;

// Transition period plus one frame to account for rounding error.
constexpr zx::duration kTestTransitionPeriod = a11y::Magnifier::kTransitionPeriod + kFramePeriod;
constexpr zx::duration kFrameEpsilon = zx::msec(1);
static_assert(kFramePeriod > kFrameEpsilon);

class MagnifierTest : public gtest::TestLoopFixture {
 public:
  MagnifierTest() { arena_.Add(&magnifier_); }

  a11y::Magnifier* magnifier() { return &magnifier_; }

  void SendPointerEvents(const std::vector<PointerParams>& events) {
    for (const auto& params : events) {
      SendPointerEvent(params);
    }
  }

 private:
  void SendPointerEvent(const PointerParams& params) {
    arena_.OnEvent(ToPointerEvent(params, input_event_time_++));
    // Run the loop to simulate a trivial passage of time. (This is realistic for everything but ADD
    // + DOWN and UP + REMOVE.)
    //
    // This covers a bug discovered during manual testing where the temporary zoom threshold timeout
    // was posted without a delay and triggered any time the third tap took nonzero time.
    RunLoopUntilIdle();
  }

  a11y::GestureArena arena_;
  a11y::Magnifier magnifier_;

  // We don't actually use these times. If we did, we'd want to more closely correlate them with
  // fake time.
  uint64_t input_event_time_ = 0;
};

// Ensure that a trigger + (temporary) pan gesture without a registered handler doesn't crash
// anything.
TEST_F(MagnifierTest, WithoutHandler) {
  SendPointerEvents(2 * TapEvents(1, {0, 0}) + DragEvents(1, {0, 0}, {.5f, 0}));
  RunLoopFor(kTestTransitionPeriod);
}

// Ensure that a trigger + (temporary) pan gesture with a closed handler doesn't crash
// anything.
TEST_F(MagnifierTest, WithClosedHandler) {
  {
    MockMagnificationHandler handler;
    magnifier()->RegisterHandler(handler.NewBinding());
    RunLoopFor(kFramePeriod);
  }

  SendPointerEvents(2 * TapEvents(1, {0, 0}) + DragEvents(1, {0, 0}, {.5f, 0}));
  RunLoopFor(kTestTransitionPeriod);
}

// Ensures that unactivated interaction does not touch a handler.
TEST_F(MagnifierTest, NoTrigger) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(DownEvents(1, {0, 0}) + MoveEvents(1, {0, 0}, {.25f, 0}, 5));
  RunLoopFor(kTestTransitionPeriod);
  // mid-interaction check
  EXPECT_EQ(handler.transform(), ClipSpaceTransform::identity());

  SendPointerEvents(MoveEvents(1, {.25f, 0}, {.5f, 0}, 5) + UpEvents(1, {.5f, 0}));
  RunLoopFor(kTestTransitionPeriod);
  // post-interaction check
  EXPECT_EQ(handler.transform(), ClipSpaceTransform::identity());
}

// Ensure that a 3x1 tap triggers magnification.
TEST_F(MagnifierTest, Trigger3x1) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(3 * TapEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);
  EXPECT_EQ(handler.transform().scale, a11y::Magnifier::kDefaultScale);
}

// Ensure that a 2x3 tap triggers magnification.
TEST_F(MagnifierTest, Trigger2x3) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(2 * Zip({TapEvents(1, {}), TapEvents(2, {}), TapEvents(3, {})}));
  RunLoopFor(kTestTransitionPeriod);
  EXPECT_EQ(handler.transform().scale, a11y::Magnifier::kDefaultScale);
}

// Ensure that a 4x1 stays magnified.
TEST_F(MagnifierTest, Trigger4x1) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(4 * TapEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);
  EXPECT_EQ(handler.transform().scale, a11y::Magnifier::kDefaultScale);
}

// Ensures that when a new handler is registered, it receives the up-to-date transform.
TEST_F(MagnifierTest, LateHandler) {
  SendPointerEvents(3 * TapEvents(1, {}));
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());
  // If there was no handler, we shouldn't have waited for the animation.
  RunLoopUntilIdle();

  EXPECT_EQ(handler.transform(), (ClipSpaceTransform{.scale = a11y::Magnifier::kDefaultScale}));
}

// This covers a bug discovered during code review where if, in between handlers, the transform is
// changed while magnified (e.g. a pan gesture is issued), the new handler would end up unmagnified.
TEST_F(MagnifierTest, InteractionBeforeLateHandler) {
  {
    MockMagnificationHandler h1;
    magnifier()->RegisterHandler(h1.NewBinding());
    SendPointerEvents(3 * TapEvents(1, {}));
    RunLoopFor(kTestTransitionPeriod);
    // Due to other bugs, this edge case only manifested if the magnification finishes
    // transitioning.
  }

  static_assert(.2f > a11y::Magnifier::kDragThreshold,
                "Need to increase jitter to exceed drag threshold.");
  // Starts with a two-finger tap, with one finger moving a little and back to where it started.
  const auto jitterDrag = Zip({DownEvents(1, {}), TapEvents(2, {})}) +
                          MoveEvents(1, {}, {.2f, .2f}) + MoveEvents(1, {.2f, .2f}, {}) +
                          UpEvents(1, {});

  // First interaction surfaces channel closure.
  SendPointerEvents(jitterDrag);
  RunLoopUntilIdle();

  // Next interaction manifests bug (zeroes out transition progress).
  SendPointerEvents(jitterDrag);
  RunLoopUntilIdle();

  MockMagnificationHandler h2;
  magnifier()->RegisterHandler(h2.NewBinding());
  RunLoopUntilIdle();

  EXPECT_EQ(h2.transform(), (ClipSpaceTransform{.scale = a11y::Magnifier::kDefaultScale}));
}

// Ensures that switching a handler causes transition updates to be delivered only to the new
// handler, still throttled at the framerate but relative to when the switch took place.
TEST_F(MagnifierTest, SwitchHandlerDuringTransition) {
  MockMagnificationHandler h1, h2;
  magnifier()->RegisterHandler(h1.NewBinding());
  SendPointerEvents(3 * TapEvents(1, {}));
  RunLoopFor(kFramePeriod * 3 / 2);
  magnifier()->RegisterHandler(h2.NewBinding());
  RunLoopUntilIdle();

  EXPECT_EQ(h1.update_count(), 2u);
  EXPECT_EQ(h2.update_count(), 1u);
  RunLoopFor(kFramePeriod - kFrameEpsilon);
  EXPECT_EQ(h2.update_count(), 1u);
  RunLoopFor(kFrameEpsilon);
  EXPECT_EQ(h1.update_count(), 2u);
  EXPECT_EQ(h2.update_count(), 2u);
}

// Ensure that a 3x1 trigger focuses on the tap coordinate.
TEST_F(MagnifierTest, TriggerFocus) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  static constexpr glm::vec2 tap_coordinate{.5f, -.25f};
  SendPointerEvents(3 * TapEvents(1, tap_coordinate));
  RunLoopFor(kTestTransitionPeriod);
  // After the final transformation, the coordinate that was tapped should still be where it was
  // before.
  EXPECT_EQ(handler.transform().Apply(tap_coordinate), tap_coordinate) << handler.transform();
}

// Ensure that a 3x1 trigger animates smoothly at the framerate.
TEST_F(MagnifierTest, TriggerTransition) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());
  // Drain the initial SetClipSpaceTransform and wait until the next frame can be presented so that
  // we can begin testing the animation right away.
  RunLoopFor(kFramePeriod);

  auto last_update_count = handler.update_count();
  float last_scale = handler.transform().scale;
  static constexpr glm::vec2 tap_coordinate{1, -1};
  SendPointerEvents(3 * TapEvents(1, tap_coordinate));
  // Since there shouldn't be a pending Present at this time, simply advancing the loop should
  // propagate the first frame of our transition. Subsequent updates will occur after every frame
  // period.
  RunLoopUntilIdle();
  for (zx::duration elapsed; elapsed < a11y::Magnifier::kTransitionPeriod;
       elapsed += kFramePeriod) {
    EXPECT_EQ(handler.update_count(), last_update_count + 1)
        << "Expect animation to be throttled at framerate.";
    EXPECT_GT(handler.transform().scale, last_scale) << elapsed;

    // The animation should still be focused on the tap coordinate.
    static constexpr float epsilon =
        std::numeric_limits<float>::epsilon() * a11y::Magnifier::kDefaultScale;
    EXPECT_TRUE(glm::all(
        glm::epsilonEqual(handler.transform().Apply(tap_coordinate), tap_coordinate, epsilon)))
        << handler.transform();

    last_scale = handler.transform().scale;
    last_update_count = handler.update_count();

    RunLoopFor(kFramePeriod);
  }

  // After the transition period, we expect the animation to stop.
  last_update_count = handler.update_count();
  RunLoopFor(kFramePeriod * 5);
  EXPECT_EQ(handler.update_count(), last_update_count);
}

// Ensure that panning during a transition integrates smoothly.
TEST_F(MagnifierTest, TransitionWithPan) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(3 * TapEvents(1, {}));
  SendPointerEvents(DownEvents(1, {}) + TapEvents(2, {}));

  // Let one frame animate so that the scale is allowed to transition past 1, which allows pan.
  // Otherwise we would expect the first translation assertion below to fail since even if a pan
  // gesture is being processed, the scale still being locked at 1 would allow no freedom to pan.
  RunLoopFor(kFramePeriod);

  ClipSpaceTransform last_transform = handler.transform();
  static_assert(a11y::Magnifier::kDragThreshold < 1.f / kDefaultMoves,
                "Need to increase drag step size to catch all moves.");
  for (const PointerParams& move_event : MoveEvents(1, {}, {-1, 1})) {
    SendPointerEvents({move_event});
    RunLoopFor(kFramePeriod);

    EXPECT_LT(handler.transform().x, last_transform.x);
    EXPECT_GT(handler.transform().y, last_transform.y);

    EXPECT_GT(handler.transform().scale, last_transform.scale);
    last_transform = handler.transform();
  }
}

// Ensure that a temporary pan during a transtion integrates smoothly and continues to focus the
// pointer.
TEST_F(MagnifierTest, TransitionWithTemporaryPan) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(2 * TapEvents(1, {}) + DownEvents(1, {}));

  // Let one frame animate so that the scale is allowed to transition past 1, which allows pan.
  // Otherwise since the tracking is relative and the first pan would still be locked, this would
  // throw off our focus and make the assertions below a lot more complicated.
  static_assert((a11y::Magnifier::kDefaultScale - 1) * a11y::Magnifier::kTransitionRate >=
                    1.f / kDefaultMoves,
                "Need to run transition further to allow drag freedom, or reduce drag step size.");
  RunLoopFor(kFramePeriod);

  float last_scale = handler.transform().scale;
  static_assert(a11y::Magnifier::kDragThreshold < 1.f / kDefaultMoves,
                "Need to increase drag step size to catch all moves.");
  for (const PointerParams& move_event : MoveEvents(1, {}, {-1, 1})) {
    SendPointerEvents({move_event});
    RunLoopFor(kFramePeriod);
    EXPECT_GT(handler.transform().scale, last_scale);
    last_scale = handler.transform().scale;

    // The animation should still be focused on the tap coordinate.
    const glm::vec2 mapped_coordinate = handler.transform().Apply(move_event.coordinate);
    static constexpr float epsilon =
        std::numeric_limits<float>::epsilon() * a11y::Magnifier::kDefaultScale;
    EXPECT_TRUE(glm::all(glm::epsilonEqual(mapped_coordinate, move_event.coordinate, epsilon)))
        << handler.transform() << ": " << mapped_coordinate << " vs. " << move_event.coordinate;
  }
}

// Ensure that panning magnification clamps to display edges, i.e. that the display area remains
// covered by content.
TEST_F(MagnifierTest, ClampPan) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  // Focus on the upper right.
  SendPointerEvents(3 * TapEvents(1, {1, -1}));
  RunLoopFor(kTestTransitionPeriod);
  const auto transform = handler.transform();

  // Now attempt to pan with a swipe towards the lower left.
  SendPointerEvents(Zip({TapEvents(1, {1, -1}), DragEvents(2, {1, -1}, {-1, 1})}));
  RunLoopFor(kFramePeriod);
  EXPECT_EQ(handler.transform(), transform) << "Clamped pan should not have moved.";
}

TEST_F(MagnifierTest, Pan) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  // Focus on the upper right.
  SendPointerEvents(3 * TapEvents(1, {1, -1}));
  RunLoopFor(kTestTransitionPeriod);
  ClipSpaceTransform transform = handler.transform();

  // Now attempt to pan with a swipe towards the upper right.
  SendPointerEvents(Zip({TapEvents(1, {-1, 1}), DragEvents(2, {-1, 1}, {1, -1})}));
  RunLoopFor(kFramePeriod);
  transform.x += 2;
  transform.y -= 2;
  EXPECT_EQ(handler.transform().scale, transform.scale);
  static constexpr float epsilon = std::numeric_limits<float>::epsilon() * kDefaultMoves;
  EXPECT_TRUE(glm::all(
      glm::epsilonEqual(handler.transform().translation(), transform.translation(), epsilon)))
      << "Expected to pan towards the lower left by -(2, -2) to " << transform.translation()
      << " (actual: " << handler.transform().translation() << ").";
}

TEST_F(MagnifierTest, PanTemporary) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  // Segue from an activation 3x1 in the upper right to a drag to the lower left.
  SendPointerEvents(2 * TapEvents(1, {1, -1}) + DownEvents(1, {1, -1}));
  RunLoopFor(kTestTransitionPeriod);
  EXPECT_EQ(handler.transform().scale, a11y::Magnifier::kDefaultScale);
  EXPECT_EQ(handler.transform().Apply({1, -1}), glm::vec2(1, -1));

  // Unlike the non-temporary pan, temporary pan should continue to focus the pointer.
  SendPointerEvents(MoveEvents(1, {1, -1}, {-1, 1}));
  RunLoopFor(kFramePeriod);
  EXPECT_EQ(handler.transform().scale, a11y::Magnifier::kDefaultScale);
  EXPECT_EQ(handler.transform().Apply({-1, 1}), glm::vec2(-1, 1));
}

TEST_F(MagnifierTest, PinchZoom) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(3 * TapEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);

  static_assert(2 * a11y::Magnifier::kDefaultScale < a11y::Magnifier::kMaxScale,
                "Need to adjust test zoom level to be less than max scale.");
  SendPointerEvents(Zip({DragEvents(1, {-.1f, 0}, {-.2f, 0}), DragEvents(2, {.1f, 0}, {.2f, 0})}));
  RunLoopFor(kFramePeriod);

  static constexpr float epsilon =
      std::numeric_limits<float>::epsilon() * 2 * a11y::Magnifier::kDefaultScale;
  EXPECT_NEAR(handler.transform().scale, 2 * a11y::Magnifier::kDefaultScale, epsilon);
}

// Ensures that after pinching zoom and toggling magnification, the magnification level is restored
// to the adjusted level.
TEST_F(MagnifierTest, RememberZoom) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(3 * TapEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);

  SendPointerEvents(Zip({DragEvents(1, {-.1f, 0}, {-.2f, 0}), DragEvents(2, {.1f, 0}, {.2f, 0})}));
  RunLoopFor(kFramePeriod);

  SendPointerEvents(3 * TapEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);
  SendPointerEvents(3 * TapEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);

  static constexpr float epsilon =
      std::numeric_limits<float>::epsilon() * 2 * a11y::Magnifier::kDefaultScale;
  EXPECT_NEAR(handler.transform().scale, 2 * a11y::Magnifier::kDefaultScale, epsilon);
}

TEST_F(MagnifierTest, MinZoom) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(3 * TapEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);

  static_assert(.1f * a11y::Magnifier::kDefaultScale < a11y::Magnifier::kMinScale,
                "Need to adjust test gesture to reach min scale.");
  SendPointerEvents(Zip({DragEvents(1, {-1, 0}, {-.1f, 0}), DragEvents(2, {1, 0}, {.1f, 0})}));
  RunLoopFor(kFramePeriod);

  EXPECT_EQ(handler.transform().scale, a11y::Magnifier::kMinScale);
}

TEST_F(MagnifierTest, MaxZoom) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(3 * TapEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);

  static_assert(a11y::Magnifier::kDefaultScale > .1f * a11y::Magnifier::kMaxScale,
                "Need to adjust test gesture to reach max scale.");
  SendPointerEvents(Zip({DragEvents(1, {-.1f, 0}, {-1, 0}), DragEvents(2, {.1f, 0}, {1, 0})}));
  RunLoopFor(kFramePeriod);

  EXPECT_EQ(handler.transform().scale, a11y::Magnifier::kMaxScale);
}

// Ensures that zooming at the edge of the screen does not violate clamping; pan should adjust to
// compensate.
TEST_F(MagnifierTest, ClampZoom) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(3 * TapEvents(1, {1, 0}));
  RunLoopFor(kTestTransitionPeriod);

  static_assert(a11y::Magnifier::kDefaultScale > 1.5f * a11y::Magnifier::kMinScale,
                "Need to adjust test zoom level to be greater than min scale.");
  SendPointerEvents(Zip({DragEvents(1, {0, -.3f}, {0, -.2f}), DragEvents(2, {0, .3f}, {0, .2f})}));
  RunLoopFor(kFramePeriod);

  static constexpr float epsilon =
      std::numeric_limits<float>::epsilon() * a11y::Magnifier::kDefaultScale / 1.5f;
  EXPECT_NEAR(handler.transform().scale, a11y::Magnifier::kDefaultScale / 1.5f, epsilon);

  // Check the anchor point to verify clamping. x should be clamped at 1. y can deviate pretty
  // wildly since it's governed by the zoom centroid, which is subject to incremental approximation.
  // While it's possible to calculate the tolerance exactly, it's not worth it.
  const glm::vec2 pt = handler.transform().Apply({1, 0});
  EXPECT_EQ(pt.x, 1);
  EXPECT_NEAR(pt.y, 0, .01f);
}

// Ensures that transitioning out of a non-default magnification animates smoothly.
TEST_F(MagnifierTest, TransitionOut) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(3 * TapEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);

  // zoom it
  SendPointerEvents(Zip({DragEvents(1, {-.1f, 0}, {-.2f, 0}), DragEvents(2, {.1f, 0}, {.2f, 0})}));
  // pan it
  SendPointerEvents(Zip({TapEvents(1, {1, -1}), DragEvents(2, {1, -1}, {-1, 1})}));

  // Zoom will issue Present immediately, so we need to wait an extra frame for the pan to be issued
  // and then for the next Present to be available.
  RunLoopFor(kFramePeriod * 2);

  ClipSpaceTransform last_transform = handler.transform();
  SendPointerEvents(3 * TapEvents(1, {}));
  RunLoopUntilIdle();
  // We expect this to restore from the pan above, which means panning +x and -y.
  for (zx::duration elapsed; elapsed < a11y::Magnifier::kTransitionPeriod;
       elapsed += kFramePeriod) {
    EXPECT_GT(handler.transform().x, last_transform.x) << elapsed;
    EXPECT_LT(handler.transform().y, last_transform.y) << elapsed;
    EXPECT_LT(handler.transform().scale, last_transform.scale) << elapsed;

    last_transform = handler.transform();

    RunLoopFor(kFramePeriod);
  }

  // After the transition period, we expect the animation to stop.
  EXPECT_EQ(handler.transform(), ClipSpaceTransform::identity());
  const auto update_count = handler.update_count();
  RunLoopFor(kFramePeriod * 5);
  EXPECT_EQ(handler.update_count(), update_count);
}

// Also include coverage for 2x3 zoom-out.
TEST_F(MagnifierTest, ZoomOut2x3) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(3 * TapEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);

  SendPointerEvents(2 * Zip({TapEvents(1, {}), TapEvents(2, {}), TapEvents(3, {})}));
  RunLoopFor(kTestTransitionPeriod);
  EXPECT_EQ(handler.transform(), ClipSpaceTransform::identity());
}

// Magnification should cease after a temporary magnification gesture is released.
TEST_F(MagnifierTest, TemporaryRelease) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(2 * TapEvents(1, {}) + DownEvents(1, {}));
  RunLoopFor(a11y::Magnifier::kTemporaryZoomHold);

  float last_scale = handler.transform().scale;
  SendPointerEvents(UpEvents(1, {}));
  RunLoopUntilIdle();
  // Go ahead and double check that we're animating the transition back out.
  for (zx::duration elapsed; elapsed < a11y::Magnifier::kTransitionPeriod;
       elapsed += kFramePeriod) {
    EXPECT_LT(handler.transform().scale, last_scale) << elapsed;
    last_scale = handler.transform().scale;

    RunLoopFor(kFramePeriod);
  }

  EXPECT_EQ(handler.transform(), ClipSpaceTransform::identity());
}

// Segueing a trigger gesture into a pan should behave as a temporary magnification.
TEST_F(MagnifierTest, TemporaryPanRelease) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(2 * TapEvents(1, {}) + DownEvents(1, {}) + MoveEvents(1, {}, {.5f, .5f}));
  RunLoopFor(kTestTransitionPeriod);

  SendPointerEvents(UpEvents(1, {.5f, .5f}));
  RunLoopFor(kTestTransitionPeriod);
  EXPECT_EQ(handler.transform(), ClipSpaceTransform::identity());
}

// Ensure that rapid input does not trigger updates faster than the framerate.
TEST_F(MagnifierTest, InputFrameThrottling) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());
  // Go ahead and send the initial SetClipSpaceTransform so that we can ensure that the initial
  // input handling below doesn't somehow schedule another Present immediately.
  RunLoopUntilIdle();

  SendPointerEvents(2 * TapEvents(1, {}) + DownEvents(1, {}) + MoveEvents(1, {}, {-1, -1}));
  RunLoopUntilIdle();
  EXPECT_EQ(handler.update_count(), 1u);
  RunLoopFor(kFramePeriod);
  EXPECT_EQ(handler.update_count(), 2u);
  RunLoopFor(kFramePeriod);
  EXPECT_EQ(handler.update_count(), 3u);
}

class MagnifierRecognizerTest : public gtest::TestLoopFixture {
 public:
  MockContestMember* member() { return &member_; }
  a11y::Magnifier* magnifier() { return &magnifier_; }

  void SendPointerEvents(const std::vector<PointerParams>& events) {
    for (const auto& params : events) {
      SendPointerEvent(params);
    }
  }

 private:
  void SendPointerEvent(const PointerParams& params) {
    if (member_.is_held()) {
      magnifier_.HandleEvent(ToPointerEvent(params, input_event_time_));
    }

    // Run the loop to simulate a trivial passage of time. (This is realistic for everything but ADD
    // + DOWN and UP + REMOVE.)
    //
    // This covers a bug discovered during manual testing where the temporary zoom threshold timeout
    // was posted without a delay and triggered any time the third tap took nonzero time.
    RunLoopUntilIdle();
    ++input_event_time_;
  }

  MockContestMember member_;
  a11y::Magnifier magnifier_;

  // We don't actually use these times. If we did, we'd want to more closely correlate them with
  // fake time.
  uint64_t input_event_time_ = 0;
};

constexpr zx::duration kTriggerEpsilon = zx::msec(1);
static_assert(a11y::Magnifier::kTriggerMaxDelay > kTriggerEpsilon);

TEST_F(MagnifierRecognizerTest, Reject1x4Immediately) {
  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(DownEvents(1, {}) + DownEvents(2, {}) + DownEvents(3, {}) + DownEvents(4, {}));
  EXPECT_EQ(member()->status(), a11y::ContestMember::Status::kRejected);
}

// 3x1 should be accepted as soon as the last tap begins and released at the end.
TEST_F(MagnifierRecognizerTest, Accept3x1) {
  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(2 * TapEvents(1, {}) + DownEvents(1, {}));
  EXPECT_EQ(member()->status(), a11y::ContestMember::Status::kAccepted);
  ASSERT_TRUE(member()->is_held());
  SendPointerEvents(UpEvents(1, {}));
  EXPECT_FALSE(member()->is_held());
}

// 2x3 should be accepted as soon as the last pointer of the last tap comes down and released at the
// end.
TEST_F(MagnifierRecognizerTest, Accept2x3) {
  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(Zip({TapEvents(1, {}), TapEvents(2, {}), TapEvents(3, {})}) +
                    DownEvents(1, {}) + DownEvents(2, {}));
  SendPointerEvents(DownEvents(3, {}));
  EXPECT_EQ(member()->status(), a11y::ContestMember::Status::kAccepted);
  SendPointerEvents(UpEvents(3, {}) + UpEvents(2, {}));
  ASSERT_TRUE(member()->is_held());
  SendPointerEvents(UpEvents(1, {}));
  EXPECT_FALSE(member()->is_held());
}

TEST_F(MagnifierRecognizerTest, Reject2x1AfterTimeout) {
  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(2 * TapEvents(1, {}));
  RunLoopFor(a11y::Magnifier::kTriggerMaxDelay - kTriggerEpsilon);
  EXPECT_TRUE(member()->is_held()) << "Boundary condition: held before timeout.";
  RunLoopFor(kTriggerEpsilon);
  EXPECT_EQ(member()->status(), a11y::ContestMember::Status::kRejected);
}

// Ensures that a 3x1 with a long wait between taps (but shorter than the timeout) is accepted.
TEST_F(MagnifierRecognizerTest, Accept3x1UnderTimeout) {
  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(TapEvents(1, {}));
  RunLoopFor(a11y::Magnifier::kTriggerMaxDelay - kTriggerEpsilon);
  SendPointerEvents(TapEvents(1, {}));
  RunLoopFor(a11y::Magnifier::kTriggerMaxDelay - kTriggerEpsilon);
  SendPointerEvents(TapEvents(1, {}));
  EXPECT_EQ(member()->status(), a11y::ContestMember::Status::kAccepted);
}

// Ensures that a long press after a 3x1 trigger is rejected after the tap timeout.
TEST_F(MagnifierRecognizerTest, Reject4x1LongPressAfterTimeout) {
  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(3 * TapEvents(1, {}));
  // At this point as verified by |Accept3x1|, we have accepted and released.
  magnifier()->OnWin();

  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  EXPECT_EQ(member()->status(), a11y::ContestMember::Status::kUndecided);
  RunLoopFor(a11y::Magnifier::kTriggerMaxDelay);
  EXPECT_EQ(member()->status(), a11y::ContestMember::Status::kRejected);
}

// Ensures that a fourth tap after a 3x1 trigger is rejected after the tap timeout.
TEST_F(MagnifierRecognizerTest, Reject4x1AfterTimeout) {
  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(3 * TapEvents(1, {}));
  // At this point as verified by |Accept3x1|, we have accepted and released.
  magnifier()->OnWin();

  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(TapEvents(1, {}));
  EXPECT_EQ(member()->status(), a11y::ContestMember::Status::kUndecided);
  RunLoopFor(a11y::Magnifier::kTriggerMaxDelay);
  EXPECT_EQ(member()->status(), a11y::ContestMember::Status::kRejected);
}

// Covers an edge regression where the second 3-tap in a zoom-out might be allowed to take forever.
TEST_F(MagnifierRecognizerTest, Reject2x3ZoomOutAfterTimeout) {
  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(3 * TapEvents(1, {}));
  // At this point as verified by |Accept3x1|, we have accepted and released.
  magnifier()->OnWin();

  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(Zip({TapEvents(1, {}), TapEvents(2, {}), TapEvents(3, {})}));
  EXPECT_EQ(member()->status(), a11y::ContestMember::Status::kUndecided);
  RunLoopFor(a11y::Magnifier::kTriggerMaxDelay);
  EXPECT_EQ(member()->status(), a11y::ContestMember::Status::kRejected);
}

TEST_F(MagnifierRecognizerTest, RejectUnmagnified1Drag) {
  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(DownEvents(1, {}) + MoveEvents(1, {}, {.25f, 0}, 1));
  EXPECT_EQ(member()->status(), a11y::ContestMember::Status::kRejected);
}

TEST_F(MagnifierRecognizerTest, RejectMagnified1Drag) {
  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(3 * TapEvents(1, {}));

  magnifier()->OnContestStarted(member()->TakeInterface());
  SendPointerEvents(DownEvents(1, {}) + MoveEvents(1, {}, {.25f, 0}, 1));
  EXPECT_EQ(member()->status(), a11y::ContestMember::Status::kRejected);
}

TEST_F(MagnifierRecognizerTest, RejectUnmagnified2Drag) {
  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(DownEvents(1, {}) + TapEvents(2, {}) + MoveEvents(1, {}, {.25f, 0}, 1));
  EXPECT_EQ(member()->status(), a11y::ContestMember::Status::kRejected);
}

TEST_F(MagnifierRecognizerTest, AcceptMagnified2Drag) {
  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(3 * TapEvents(1, {}));
  magnifier()->OnWin();

  magnifier()->OnContestStarted(member()->TakeInterface());
  SendPointerEvents(DownEvents(1, {}) + TapEvents(2, {}) + MoveEvents(1, {}, {.25f, 0}, 1));
  EXPECT_EQ(member()->status(), a11y::ContestMember::Status::kAccepted);
  ASSERT_TRUE(member()->is_held());
  SendPointerEvents(UpEvents(1, {}));
  EXPECT_FALSE(member()->is_held());
}

TEST_F(MagnifierRecognizerTest, RejectUnmagnified1LongPress) {
  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  RunLoopFor(a11y::Magnifier::kTriggerMaxDelay);
  EXPECT_EQ(member()->status(), a11y::ContestMember::Status::kRejected);
}

TEST_F(MagnifierRecognizerTest, RejectMagnified1LongPress) {
  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(3 * TapEvents(1, {}));
  magnifier()->OnWin();

  magnifier()->OnContestStarted(member()->TakeInterface());
  SendPointerEvents(DownEvents(1, {}));
  RunLoopFor(a11y::Magnifier::kTriggerMaxDelay);
  EXPECT_EQ(member()->status(), a11y::ContestMember::Status::kRejected);
}

TEST_F(MagnifierRecognizerTest, RejectUnmagnified2LongPress) {
  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(DownEvents(1, {}) + DownEvents(2, {}));
  RunLoopFor(a11y::Magnifier::kTriggerMaxDelay);
  EXPECT_EQ(member()->status(), a11y::ContestMember::Status::kRejected);
}

// Ensures that transitions don't happen until we've won.
TEST_F(MagnifierRecognizerTest, TriggerWaitForWin) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());
  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(3 * TapEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);
  EXPECT_EQ(handler.transform(), ClipSpaceTransform::identity());

  magnifier()->OnWin();
  RunLoopFor(kTestTransitionPeriod);
  EXPECT_EQ(handler.transform().scale, a11y::Magnifier::kDefaultScale);
}

// Ensures that if another recognizer wins after we accept, magnifier does not enable.
TEST_F(MagnifierRecognizerTest, AbortOnLoss) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());
  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(3 * TapEvents(1, {}));

  magnifier()->OnDefeat();
  RunLoopFor(kTestTransitionPeriod);
  EXPECT_EQ(handler.transform(), ClipSpaceTransform::identity());
}

// Ensures that drags don't start until we've won.
TEST_F(MagnifierRecognizerTest, PanWaitForWin) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());
  magnifier()->OnContestStarted(member()->TakeInterface());

  SendPointerEvents(3 * TapEvents(1, {}));
  magnifier()->OnWin();

  RunLoopFor(kTestTransitionPeriod);

  ClipSpaceTransform transform = handler.transform();

  magnifier()->OnContestStarted(member()->TakeInterface());
  SendPointerEvents(Zip({DownEvents(1, {}), TapEvents(2, {})}) + MoveEvents(1, {}, {.5f, .5f}));

  RunLoopFor(kFramePeriod);
  EXPECT_EQ(handler.transform(), transform);

  magnifier()->OnWin();

  // There are at least two or three reasonable interpretations here:
  // * buffer the pan until we win and then snap to the most up-to-date position
  // * delay accumulation until we win
  // * buffer the pan until we win and transition smoothly to the most up-to-date position
  // For simplicity and consistency with trigger gestures, we pick the first for now. In practice
  // the win should be awarded almost immediately for the magnifier if it is competing against
  // screen reader.

  RunLoopFor(kFramePeriod);
  transform.x = .5f;
  transform.y = .5f;
  EXPECT_EQ(handler.transform(), transform);

  SendPointerEvents(MoveEvents(1, {.5f, .5f}, {1, 1}));
  RunLoopFor(kFramePeriod);
  transform.x = 1;
  transform.y = 1;
  EXPECT_EQ(handler.transform(), transform);
}

}  // namespace
}  // namespace accessibility_test
