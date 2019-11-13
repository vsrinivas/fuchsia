// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/magnifier/magnifier.h"

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/zx/time.h>

#include <limits>
#include <optional>

#include <gtest/gtest.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/a11y/lib/gesture_manager/arena/gesture_arena.h"
#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"
#include "src/ui/a11y/lib/magnifier/tests/mocks/mock_handler.h"
#include "src/ui/a11y/lib/magnifier/tests/util/clip_space_transform.h"
#include "src/ui/a11y/lib/magnifier/tests/util/formatting.h"
#include "src/ui/a11y/lib/magnifier/tests/util/input.h"
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

// A passive recognizer to obtain an arena member to inspect/affect arena state.
class TestRecognizer : public a11y::GestureRecognizer {
 public:
  void HandleEvent(const fuchsia::ui::input::accessibility::PointerEvent&) override {}
  std::string DebugName() const override { return "TestRecognizer"; }
};

class MagnifierTest : public gtest::TestLoopFixture {
 public:
  MagnifierTest()
      : arena_([this](auto, auto, EventHandling handled) { input_handling_ = handled; }) {
    magnifier_.arena_member(arena_.Add(&magnifier_));
    test_arena_member_ = arena_.Add(&test_recognizer_);
  }

  a11y::Magnifier* magnifier() { return &magnifier_; }

  std::optional<EventHandling>& input_handling() { return input_handling_; }

  bool is_arena_contending() const {
    return test_arena_member_->status() == a11y::ArenaMember::Status::kContending;
  }

  bool is_arena_ceded() const {
    return test_arena_member_->status() == a11y::ArenaMember::Status::kWinner;
  }

  // Causes the test arena member to win the arena, denying the gesture to Magnifier.
  void DenyArena() {
    test_arena_member_->Hold();
    test_arena_member_->Accept();
  }

  void ResetArena() { test_arena_member_->Release(); }

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

  TestRecognizer test_recognizer_;
  a11y::GestureArena arena_;

  a11y::Magnifier magnifier_;
  a11y::ArenaMember* test_arena_member_;

  // We don't actually use these times. If we did, we'd want to more closely correlate them with
  // fake time.
  uint64_t input_event_time_ = 0;
  std::optional<EventHandling> input_handling_;
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
    MockHandler handler;
    magnifier()->RegisterHandler(handler.NewBinding());
    RunLoopFor(kFramePeriod);
  }

  SendPointerEvents(2 * TapEvents(1, {0, 0}) + DragEvents(1, {0, 0}, {.5f, 0}));
  RunLoopFor(kTestTransitionPeriod);
}

// Ensures that unactivated interaction does not touch a handler.
TEST_F(MagnifierTest, NoTrigger) {
  MockHandler handler;
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
  MockHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(3 * TapEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);
  EXPECT_EQ(handler.transform().scale, a11y::Magnifier::kDefaultScale);
}

// Ensure that a 2x3 tap triggers magnification.
TEST_F(MagnifierTest, Trigger2x3) {
  MockHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(2 * zip({TapEvents(1, {}), TapEvents(2, {}), TapEvents(3, {})}));
  RunLoopFor(kTestTransitionPeriod);
  EXPECT_EQ(handler.transform().scale, a11y::Magnifier::kDefaultScale);
}

// Ensure that a 4x1 stays magnified.
TEST_F(MagnifierTest, Trigger4x1) {
  MockHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(4 * TapEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);
  EXPECT_EQ(handler.transform().scale, a11y::Magnifier::kDefaultScale);
}

// Ensures that when a new handler is registered, it receives the up-to-date transform.
TEST_F(MagnifierTest, LateHandler) {
  SendPointerEvents(3 * TapEvents(1, {}));
  MockHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());
  // If there was no handler, we shouldn't have waited for the animation.
  RunLoopUntilIdle();

  EXPECT_EQ(handler.transform(), (ClipSpaceTransform{.scale = a11y::Magnifier::kDefaultScale}));
}

// This covers a bug discovered during code review where if, in between handlers, the transform is
// changed while magnified (e.g. a pan gesture is issued), the new handler would end up unmagnified.
TEST_F(MagnifierTest, InteractionBeforeLateHandler) {
  {
    MockHandler h1;
    magnifier()->RegisterHandler(h1.NewBinding());
    SendPointerEvents(3 * TapEvents(1, {}));
    RunLoopFor(kTestTransitionPeriod);
    // Due to other bugs, this edge case only manifested if the magnification finishes
    // transitioning.
  }

  static_assert(.2f > a11y::Magnifier::kDragThreshold,
                "Need to increase jitter to exceed drag threshold.");
  // Starts with a two-finger tap, with one finger moving a little and back to where it started.
  const auto jitterDrag = zip({DownEvents(1, {}), TapEvents(2, {})}) +
                          MoveEvents(1, {}, {.2f, .2f}) + MoveEvents(1, {.2f, .2f}, {}) +
                          UpEvents(1, {});

  // First interaction surfaces channel closure.
  SendPointerEvents(jitterDrag);
  RunLoopUntilIdle();

  // Next interaction manifests bug (zeroes out transition progress).
  SendPointerEvents(jitterDrag);
  RunLoopUntilIdle();

  MockHandler h2;
  magnifier()->RegisterHandler(h2.NewBinding());
  RunLoopUntilIdle();

  EXPECT_EQ(h2.transform(), (ClipSpaceTransform{.scale = a11y::Magnifier::kDefaultScale}));
}

// Ensures that switching a handler causes transition updates to be delivered only to the new
// handler, still throttled at the framerate but relative to when the switch took place.
TEST_F(MagnifierTest, SwitchHandlerDuringTransition) {
  MockHandler h1, h2;
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
  MockHandler handler;
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
  MockHandler handler;
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
  MockHandler handler;
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
  MockHandler handler;
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
  MockHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  // Focus on the upper right.
  SendPointerEvents(3 * TapEvents(1, {1, -1}));
  RunLoopFor(kTestTransitionPeriod);
  const auto transform = handler.transform();

  // Now attempt to pan with a swipe towards the lower left.
  SendPointerEvents(zip({TapEvents(1, {1, -1}), DragEvents(2, {1, -1}, {-1, 1})}));
  RunLoopFor(kFramePeriod);
  EXPECT_EQ(handler.transform(), transform) << "Clamped pan should not have moved.";
}

TEST_F(MagnifierTest, Pan) {
  MockHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  // Focus on the upper right.
  SendPointerEvents(3 * TapEvents(1, {1, -1}));
  RunLoopFor(kTestTransitionPeriod);
  ClipSpaceTransform transform = handler.transform();

  // Now attempt to pan with a swipe towards the upper right.
  SendPointerEvents(zip({TapEvents(1, {-1, 1}), DragEvents(2, {-1, 1}, {1, -1})}));
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
  MockHandler handler;
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
  MockHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(3 * TapEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);

  static_assert(2 * a11y::Magnifier::kDefaultScale < a11y::Magnifier::kMaxScale,
                "Need to adjust test zoom level to be less than max scale.");
  SendPointerEvents(zip({DragEvents(1, {-.1f, 0}, {-.2f, 0}), DragEvents(2, {.1f, 0}, {.2f, 0})}));
  RunLoopFor(kFramePeriod);

  static constexpr float epsilon =
      std::numeric_limits<float>::epsilon() * 2 * a11y::Magnifier::kDefaultScale;
  EXPECT_NEAR(handler.transform().scale, 2 * a11y::Magnifier::kDefaultScale, epsilon);
}

// Ensures that after pinching zoom and toggling magnification, the magnification level is restored
// to the adjusted level.
TEST_F(MagnifierTest, RememberZoom) {
  MockHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(3 * TapEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);

  SendPointerEvents(zip({DragEvents(1, {-.1f, 0}, {-.2f, 0}), DragEvents(2, {.1f, 0}, {.2f, 0})}));
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
  MockHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(3 * TapEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);

  static_assert(.1f * a11y::Magnifier::kDefaultScale < a11y::Magnifier::kMinScale,
                "Need to adjust test gesture to reach min scale.");
  SendPointerEvents(zip({DragEvents(1, {-1, 0}, {-.1f, 0}), DragEvents(2, {1, 0}, {.1f, 0})}));
  RunLoopFor(kFramePeriod);

  EXPECT_EQ(handler.transform().scale, a11y::Magnifier::kMinScale);
}

TEST_F(MagnifierTest, MaxZoom) {
  MockHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(3 * TapEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);

  static_assert(a11y::Magnifier::kDefaultScale > .1f * a11y::Magnifier::kMaxScale,
                "Need to adjust test gesture to reach max scale.");
  SendPointerEvents(zip({DragEvents(1, {-.1f, 0}, {-1, 0}), DragEvents(2, {.1f, 0}, {1, 0})}));
  RunLoopFor(kFramePeriod);

  EXPECT_EQ(handler.transform().scale, a11y::Magnifier::kMaxScale);
}

// Ensures that zooming at the edge of the screen does not violate clamping; pan should adjust to
// compensate.
TEST_F(MagnifierTest, ClampZoom) {
  MockHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(3 * TapEvents(1, {1, 0}));
  RunLoopFor(kTestTransitionPeriod);

  static_assert(a11y::Magnifier::kDefaultScale > 1.5f * a11y::Magnifier::kMinScale,
                "Need to adjust test zoom level to be greater than min scale.");
  SendPointerEvents(zip({DragEvents(1, {0, -.3f}, {0, -.2f}), DragEvents(2, {0, .3f}, {0, .2f})}));
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
  MockHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(3 * TapEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);

  // zoom it
  SendPointerEvents(zip({DragEvents(1, {-.1f, 0}, {-.2f, 0}), DragEvents(2, {.1f, 0}, {.2f, 0})}));
  // pan it
  SendPointerEvents(zip({TapEvents(1, {1, -1}), DragEvents(2, {1, -1}, {-1, 1})}));

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

// Magnification should cease after a temporary magnification gesture is released.
TEST_F(MagnifierTest, TemporaryRelease) {
  MockHandler handler;
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
  MockHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(2 * TapEvents(1, {}) + DownEvents(1, {}) + MoveEvents(1, {}, {.5f, .5f}));
  RunLoopFor(kTestTransitionPeriod);

  SendPointerEvents(UpEvents(1, {.5f, .5f}));
  RunLoopFor(kTestTransitionPeriod);
  EXPECT_EQ(handler.transform(), ClipSpaceTransform::identity());
}

// Ensure that rapid input does not trigger updates faster than the framerate.
TEST_F(MagnifierTest, InputFrameThrottling) {
  MockHandler handler;
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

using MagnifierArenaTest = MagnifierTest;

constexpr zx::duration kTriggerEpsilon = zx::msec(1);
static_assert(a11y::Magnifier::kTriggerMaxDelay > kTriggerEpsilon);

TEST_F(MagnifierArenaTest, Reject1x4Immediately) {
  SendPointerEvents(DownEvents(1, {}) + DownEvents(2, {}) + DownEvents(3, {}) + DownEvents(4, {}));
  EXPECT_TRUE(is_arena_ceded());
}

// TODO(40943): In several cases below, input_handling() would ideally be CONSUMED earlier. When we
// support that, we might also want to add another state assertion at the end of interactions, if
// that's our responsibility.

// 3x1 should be accepted as soon as the last tap begins.
TEST_F(MagnifierArenaTest, Accept3x1) {
  SendPointerEvents(2 * TapEvents(1, {}) + DownEvents(1, {}));
  EXPECT_FALSE(is_arena_contending());
  SendPointerEvents(UpEvents(1, {}));
  EXPECT_EQ(input_handling(), EventHandling::CONSUMED);
}

// 2x3 should be accepted as soon as the last pointer of the last tap comes down.
TEST_F(MagnifierArenaTest, Accept2x3) {
  SendPointerEvents(zip({TapEvents(1, {}), TapEvents(2, {}), TapEvents(3, {})}) +
                    DownEvents(1, {}) + DownEvents(2, {}));
  EXPECT_FALSE(input_handling());
  SendPointerEvents(DownEvents(3, {}));
  EXPECT_FALSE(is_arena_contending());
  SendPointerEvents(UpEvents(3, {}) + UpEvents(2, {}));
  EXPECT_FALSE(is_arena_contending());
  SendPointerEvents(UpEvents(1, {}));
  EXPECT_EQ(input_handling(), EventHandling::CONSUMED);
}

TEST_F(MagnifierArenaTest, Reject2x1AfterTimeout) {
  SendPointerEvents(2 * TapEvents(1, {}));
  RunLoopFor(a11y::Magnifier::kTriggerMaxDelay - kTriggerEpsilon);
  EXPECT_TRUE(is_arena_contending()) << "Boundary condition: held before timeout.";
  RunLoopFor(kTriggerEpsilon);
  EXPECT_TRUE(is_arena_ceded());
}

// Ensures that a 3x1 with a long wait between taps (but shorter than the timeout) is accepted.
TEST_F(MagnifierArenaTest, Accept3x1UnderTimeout) {
  SendPointerEvents(TapEvents(1, {}));
  RunLoopFor(a11y::Magnifier::kTriggerMaxDelay - kTriggerEpsilon);
  SendPointerEvents(TapEvents(1, {}));
  RunLoopFor(a11y::Magnifier::kTriggerMaxDelay - kTriggerEpsilon);
  SendPointerEvents(TapEvents(1, {}));
  EXPECT_EQ(input_handling(), EventHandling::CONSUMED);
}

// Ensures that a long press after a 3x1 trigger is rejected after the tap timeout.
TEST_F(MagnifierArenaTest, Reject4x1LongPressAfterTimeout) {
  SendPointerEvents(3 * TapEvents(1, {}) + DownEvents(1, {}));
  EXPECT_TRUE(is_arena_contending());
  RunLoopFor(a11y::Magnifier::kTriggerMaxDelay);
  EXPECT_TRUE(is_arena_ceded());
}

// Ensures that a fourth tap after a 3x1 trigger is rejected after the tap timeout.
TEST_F(MagnifierArenaTest, Reject4x1AfterTimeout) {
  SendPointerEvents(4 * TapEvents(1, {}));
  EXPECT_TRUE(is_arena_contending());
  RunLoopFor(a11y::Magnifier::kTriggerMaxDelay);
  EXPECT_TRUE(is_arena_ceded());
}

TEST_F(MagnifierArenaTest, RejectUnmagnified1Drag) {
  SendPointerEvents(DownEvents(1, {}) + MoveEvents(1, {}, {.25f, 0}, 1));
  EXPECT_TRUE(is_arena_ceded());
}

TEST_F(MagnifierArenaTest, RejectMagnified1Drag) {
  SendPointerEvents(3 * TapEvents(1, {}));
  SendPointerEvents(DownEvents(1, {}) + MoveEvents(1, {}, {.25f, 0}, 1));
  EXPECT_TRUE(is_arena_ceded());
}

TEST_F(MagnifierArenaTest, RejectUnmagnified2Drag) {
  SendPointerEvents(DownEvents(1, {}) + TapEvents(2, {}) + MoveEvents(1, {}, {.25f, 0}, 1));
  EXPECT_TRUE(is_arena_ceded());
}

TEST_F(MagnifierArenaTest, AcceptMagnified2Drag) {
  SendPointerEvents(3 * TapEvents(1, {}));
  input_handling() = std::nullopt;

  SendPointerEvents(DownEvents(1, {}) + TapEvents(2, {}) + MoveEvents(1, {}, {.25f, 0}, 1));
  EXPECT_FALSE(is_arena_contending());
  SendPointerEvents(UpEvents(1, {}));
  EXPECT_EQ(input_handling(), EventHandling::CONSUMED);
}

TEST_F(MagnifierArenaTest, RejectUnmagnified1LongPress) {
  SendPointerEvents(DownEvents(1, {}));
  RunLoopFor(a11y::Magnifier::kTriggerMaxDelay);
  EXPECT_TRUE(is_arena_ceded());
}

TEST_F(MagnifierArenaTest, RejectMagnified1LongPress) {
  SendPointerEvents(3 * TapEvents(1, {}));

  SendPointerEvents(DownEvents(1, {}));
  RunLoopFor(a11y::Magnifier::kTriggerMaxDelay);
  EXPECT_TRUE(is_arena_ceded());
}

TEST_F(MagnifierArenaTest, RejectUnmagnified2LongPress) {
  SendPointerEvents(DownEvents(1, {}) + DownEvents(2, {}));
  RunLoopFor(a11y::Magnifier::kTriggerMaxDelay);
  EXPECT_TRUE(is_arena_ceded());
}

TEST_F(MagnifierArenaTest, RejectMagnified2LongPress) {
  SendPointerEvents(3 * TapEvents(1, {}));
  input_handling() = std::nullopt;

  SendPointerEvents(DownEvents(1, {}) + DownEvents(2, {}));
  EXPECT_FALSE(is_arena_contending());
  SendPointerEvents(UpEvents(1, {}));
  EXPECT_FALSE(is_arena_contending());
  SendPointerEvents(UpEvents(2, {}));
  EXPECT_EQ(input_handling(), EventHandling::CONSUMED);
}

// Ensures that if another recognizer claims an in-progress gesture, magnifier does not magnify, and
// its recognizer resets for the next gesture.
TEST_F(MagnifierArenaTest, InterruptingCowBetweenTaps) {
  MockHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(2 * TapEvents(1, {}));
  DenyArena();
  SendPointerEvents(TapEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);

  EXPECT_EQ(handler.transform(), ClipSpaceTransform::identity());
  ResetArena();

  SendPointerEvents(2 * TapEvents(1, {}));
  RunLoopFor(kFramePeriod);
  // Make sure we don't jump the gun.
  EXPECT_EQ(handler.transform(), ClipSpaceTransform::identity());
  SendPointerEvents(TapEvents(1, {}));
  RunLoopUntilIdle();
  EXPECT_GT(handler.transform().scale, 1);
}

// Ensures that if another recognizer claims an in-progress gesture, magnifier does not magnify, and
// its recognizer resets for the next gesture.
TEST_F(MagnifierArenaTest, InterruptingCowWithinTap) {
  MockHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());

  SendPointerEvents(TapEvents(1, {}) + DownEvents(1, {}));
  DenyArena();
  SendPointerEvents(UpEvents(1, {}));
  RunLoopFor(kTestTransitionPeriod);

  EXPECT_EQ(handler.transform(), ClipSpaceTransform::identity());
  ResetArena();

  SendPointerEvents(2 * TapEvents(1, {}));
  RunLoopFor(kFramePeriod);
  // Make sure we don't jump the gun.
  EXPECT_EQ(handler.transform(), ClipSpaceTransform::identity());
  SendPointerEvents(TapEvents(1, {}));
  RunLoopUntilIdle();
  EXPECT_GT(handler.transform().scale, 1);
}

// Ensures that if another recognizer claims a series of taps and then another begins, we don't
// train wreck on tap timeouts.
TEST_F(MagnifierArenaTest, InterruptingCowWithinTimeout) {
  SendPointerEvents(2 * TapEvents(1, {}));
  RunLoopFor(a11y::Magnifier::kTriggerMaxDelay * 2 / 3);
  DenyArena();
  ResetArena();

  SendPointerEvents(DownEvents(1, {}));
  // At this point, if we're train-wrecking on timeouts, the last one would expire in 1/3 of the
  // expected duration. If by 2/3 of the expected duration we've spuriously rejected, we know we're
  // making a mistake.
  RunLoopFor(a11y::Magnifier::kTriggerMaxDelay * 2 / 3);
  EXPECT_TRUE(is_arena_contending());
}

}  // namespace

}  // namespace accessibility_test
