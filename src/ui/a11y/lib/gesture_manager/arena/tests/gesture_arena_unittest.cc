// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/arena/gesture_arena.h"

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>

#include <memory>
#include <vector>

namespace a11y {
namespace {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;

class MockGestureRecognizer : public GestureRecognizer {
 public:
  MockGestureRecognizer() = default;
  ~MockGestureRecognizer() = default;

  void OnWin() override { won_ = true; }

  bool OnWinWasCalled() { return won_; }

  void OnDefeat() override { lost_ = true; }

  bool OnDefeatWasCalled() { return lost_; }

  void HandleEvent(const AccessibilityPointerEvent& pointer_event) override { num_events_++; }

  uint32_t num_events() { return num_events_; }

  void OnContestStarted() override { contest_started_ = true; }

  bool OnContestStartedWasCalled() const { return contest_started_; }

  std::string DebugName() const override { return "mock_gesture_recognizer"; }

  void Reset() {
    won_ = false;
    lost_ = false;
    contest_started_ = false;
    num_events_ = 0;
  }

 private:
  bool won_ = false;
  bool lost_ = false;
  bool contest_started_ = false;
  uint32_t num_events_ = 0;
};

using GestureArenaTest = gtest::TestLoopFixture;

constexpr uint32_t kDefaultDeviceID = 42;
constexpr uint32_t kDefaultPointerID = 1;

// Returns a default Accessibility Pointer Event.
AccessibilityPointerEvent GetDefaultPointerEvent() {
  AccessibilityPointerEvent event;
  event.set_event_time(10);
  event.set_device_id(kDefaultDeviceID);
  event.set_pointer_id(kDefaultPointerID);
  event.set_type(fuchsia::ui::input::PointerEventType::TOUCH);
  event.set_phase(Phase::ADD);
  event.set_ndc_point({4, 4});
  event.set_viewref_koid(100);
  event.set_local_point({2, 2});
  return event;
}

TEST_F(GestureArenaTest, SingleContenderWins) {
  GestureArena arena([](auto...) {});
  MockGestureRecognizer recognizer;
  auto* member = arena.Add(&recognizer);
  auto event = GetDefaultPointerEvent();
  arena.OnEvent(event);
  EXPECT_EQ(member->status(), ArenaMember::Status::kWinner);
}

TEST_F(GestureArenaTest, AllMembersAreContendingOnAddEvent) {
  GestureArena arena([](auto...) {});
  MockGestureRecognizer recognizer_1;
  MockGestureRecognizer recognizer_2;
  MockGestureRecognizer recognizer_3;
  auto* member_1 = arena.Add(&recognizer_1);
  auto* member_2 = arena.Add(&recognizer_2);
  auto* member_3 = arena.Add(&recognizer_3);
  arena.OnEvent(GetDefaultPointerEvent());

  EXPECT_EQ(member_1->status(), ArenaMember::Status::kContending);
  EXPECT_EQ(member_2->status(), ArenaMember::Status::kContending);
  EXPECT_EQ(member_3->status(), ArenaMember::Status::kContending);
}

TEST_F(GestureArenaTest, FirstContenderClaimVictoryWins) {
  GestureArena arena([](auto...) {});
  MockGestureRecognizer recognizer_1;
  MockGestureRecognizer recognizer_2;
  auto* member_1 = arena.Add(&recognizer_1);
  auto* member_2 = arena.Add(&recognizer_2);
  arena.OnEvent(GetDefaultPointerEvent());

  arena.OnEvent(GetDefaultPointerEvent());
  EXPECT_TRUE(member_1->Accept());
  EXPECT_TRUE(recognizer_1.OnWinWasCalled());
  EXPECT_TRUE(recognizer_2.OnDefeatWasCalled());
  EXPECT_EQ(member_1->status(), ArenaMember::Status::kWinner);
  EXPECT_EQ(member_2->status(), ArenaMember::Status::kDefeated);
}

TEST_F(GestureArenaTest, SecondContenderClaimVictoryFails) {
  GestureArena arena([](auto...) {});
  MockGestureRecognizer recognizer_1;
  MockGestureRecognizer recognizer_2;
  auto* member_1 = arena.Add(&recognizer_1);
  auto* member_2 = arena.Add(&recognizer_2);

  arena.OnEvent(GetDefaultPointerEvent());

  EXPECT_TRUE(member_1->Accept());
  EXPECT_FALSE(member_2->Accept());
  EXPECT_TRUE(recognizer_1.OnWinWasCalled());
  EXPECT_TRUE(recognizer_2.OnDefeatWasCalled());
  EXPECT_EQ(member_1->status(), ArenaMember::Status::kWinner);
  EXPECT_EQ(member_2->status(), ArenaMember::Status::kDefeated);
}

TEST_F(GestureArenaTest, LastStandingWins) {
  GestureArena arena([](auto...) {});
  MockGestureRecognizer recognizer_1;
  MockGestureRecognizer recognizer_2;
  MockGestureRecognizer recognizer_3;
  auto* member_1 = arena.Add(&recognizer_1);
  auto* member_2 = arena.Add(&recognizer_2);
  auto* member_3 = arena.Add(&recognizer_3);

  arena.OnEvent(GetDefaultPointerEvent());

  EXPECT_EQ(member_3->status(), ArenaMember::Status::kContending);

  member_1->Reject();
  EXPECT_TRUE(recognizer_1.OnDefeatWasCalled());

  EXPECT_EQ(member_1->status(), ArenaMember::Status::kDefeated);
  EXPECT_EQ(member_2->status(), ArenaMember::Status::kContending);
  EXPECT_EQ(member_3->status(), ArenaMember::Status::kContending);

  member_3->Reject();
  EXPECT_TRUE(recognizer_3.OnDefeatWasCalled());
  EXPECT_TRUE(recognizer_2.OnWinWasCalled());
  EXPECT_EQ(member_2->status(), ArenaMember::Status::kWinner);
  EXPECT_EQ(member_3->status(), ArenaMember::Status::kDefeated);
}

TEST_F(GestureArenaTest, RoutePointerEvents) {
  // This test makes sure that pointer events are sent to all active arena
  // members, either because they are still contending or they haven't called Reject() yet.
  uint32_t actual_device_id = 0;
  uint32_t actual_pointer_id = 0;
  fuchsia::ui::input::accessibility::EventHandling actual_handled_1 =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;
  GestureArena arena([&actual_device_id, &actual_pointer_id, &actual_handled_1](
                         uint32_t device_id, uint32_t pointer_id,
                         fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id = device_id;
    actual_pointer_id = pointer_id;
    actual_handled_1 = handled;
  });
  MockGestureRecognizer recognizer_1;
  MockGestureRecognizer recognizer_2;
  auto* member_1 = arena.Add(&recognizer_1);
  auto* member_2 = arena.Add(&recognizer_2);
  {
    // ADD event, will have a callback later indicating weather the pointer event stream was
    // consumed or rejected.
    auto event = GetDefaultPointerEvent();
    arena.OnEvent(event);
  }

  EXPECT_EQ(recognizer_1.num_events(), 1u);
  EXPECT_EQ(recognizer_2.num_events(), 1u);

  {
    // DOWN event, will  not have the callback invoked.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    arena.OnEvent(event);
  }

  EXPECT_EQ(recognizer_1.num_events(), 2u);
  EXPECT_EQ(recognizer_2.num_events(), 2u);

  EXPECT_TRUE(member_1->Accept());
  EXPECT_TRUE(recognizer_1.OnWinWasCalled());
  EXPECT_TRUE(recognizer_2.OnDefeatWasCalled());
  EXPECT_EQ(member_1->status(), ArenaMember::Status::kWinner);
  EXPECT_EQ(member_2->status(), ArenaMember::Status::kDefeated);

  {
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::UP);
    arena.OnEvent(event);
  }

  EXPECT_EQ(recognizer_1.num_events(), 3u);
  // recognizer_2 hasn't called Reject() on its own, so it still receives events.
  EXPECT_EQ(recognizer_2.num_events(), 3u);

  member_2->Reject();

  {
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::REMOVE);
    arena.OnEvent(event);
  }

  EXPECT_EQ(recognizer_1.num_events(), 4u);
  // recognizer_2 called Reject(), it no longer receives events.
  EXPECT_EQ(recognizer_2.num_events(), 3u);

  // The interaction ended, check callbacks.
  EXPECT_EQ(actual_handled_1, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_EQ(actual_device_id, kDefaultDeviceID);
  EXPECT_EQ(actual_pointer_id, kDefaultPointerID);
}

TEST_F(GestureArenaTest, EmptyArenaConsumesPointerEvents) {
  // This test makes sure that when the arena is empty and configured to consume pointer events, the
  // input system gets the appropriate callback.
  uint32_t actual_device_id = 0;
  uint32_t actual_pointer_id = 0;
  fuchsia::ui::input::accessibility::EventHandling actual_handled_1 =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;
  GestureArena arena(
      [&actual_device_id, &actual_pointer_id, &actual_handled_1](
          uint32_t device_id, uint32_t pointer_id,
          fuchsia::ui::input::accessibility::EventHandling handled) {
        actual_device_id = device_id;
        actual_pointer_id = pointer_id;
        actual_handled_1 = handled;
      },
      GestureArena::EventHandlingPolicy::kConsumeEvents);
  MockGestureRecognizer recognizer_1;
  auto* member_1 = arena.Add(&recognizer_1);

  {
    // ADD event, will have a callback later indicating weather the pointer event stream was
    // consumed or rejected.
    auto event = GetDefaultPointerEvent();
    arena.OnEvent(event);
  }

  // Single contender always wins.
  EXPECT_EQ(member_1->status(), ArenaMember::Status::kWinner);
  member_1->Reject();  // The arena becomes empty.
  EXPECT_EQ(member_1->status(), ArenaMember::Status::kDefeated);
  EXPECT_TRUE(recognizer_1.OnDefeatWasCalled());

  // The input system should see the callback now, as the arena is empty.
  EXPECT_EQ(actual_handled_1, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_EQ(actual_device_id, kDefaultDeviceID);
  EXPECT_EQ(actual_pointer_id, kDefaultPointerID);

  EXPECT_EQ(recognizer_1.num_events(), 1u);

  // Continue with the sequence of events, until the interaction is over.
  {
    // DOWN event, will  not have the callback invoked.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    arena.OnEvent(event);
  }

  EXPECT_EQ(recognizer_1.num_events(), 1u);

  {
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::REMOVE);
    arena.OnEvent(event);
  }

  EXPECT_EQ(recognizer_1.num_events(), 1u);
}

TEST_F(GestureArenaTest, EmptyArenaRejectsPointerEvents) {
  // This test makes sure that when the arena is empty and configured to reject pointer events, the
  // input system gets the appropriate callback.
  fuchsia::ui::input::accessibility::EventHandling actual_handled_1 =
      fuchsia::ui::input::accessibility::EventHandling::CONSUMED;
  GestureArena arena(
      [&actual_handled_1](uint32_t device_id, uint32_t pointer_id,
                          fuchsia::ui::input::accessibility::EventHandling handled) {
        actual_handled_1 = handled;
      },
      GestureArena::EventHandlingPolicy::kRejectEvents);
  MockGestureRecognizer recognizer_1;
  auto* member_1 = arena.Add(&recognizer_1);

  {
    // ADD event, will have a callback later indicating weather the pointer event stream was
    // consumed or rejected.
    auto event = GetDefaultPointerEvent();
    arena.OnEvent(event);
  }

  // Single contender always wins.
  EXPECT_EQ(member_1->status(), ArenaMember::Status::kWinner);
  member_1->Reject();  // The arena becomes empty.
  EXPECT_EQ(member_1->status(), ArenaMember::Status::kDefeated);
  EXPECT_TRUE(recognizer_1.OnDefeatWasCalled());

  // The input system should see the callback now, as the arena is empty.
  EXPECT_EQ(actual_handled_1, fuchsia::ui::input::accessibility::EventHandling::REJECTED);

  EXPECT_EQ(recognizer_1.num_events(), 1u);
  // Unlike the test above, input system does not send more events to us, so the interaction is
  // over.
}

TEST_F(GestureArenaTest, ContendersAreNotifiedWhenContendingStarts) {
  GestureArena arena([](auto...) {});
  MockGestureRecognizer recognizer_1;
  MockGestureRecognizer recognizer_2;
  arena.Add(&recognizer_1);
  arena.Add(&recognizer_2);
  arena.OnEvent(GetDefaultPointerEvent());

  EXPECT_TRUE(recognizer_1.OnContestStartedWasCalled());
  EXPECT_TRUE(recognizer_2.OnContestStartedWasCalled());

  {
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::REMOVE);
    arena.OnEvent(event);
  }

  recognizer_1.Reset();
  recognizer_2.Reset();

  arena.OnEvent(GetDefaultPointerEvent());

  EXPECT_TRUE(recognizer_1.OnContestStartedWasCalled());
  EXPECT_TRUE(recognizer_2.OnContestStartedWasCalled());
}

TEST_F(GestureArenaTest, DoNotCallOnContendingStartedWhenArenaIsHeld) {
  GestureArena arena([](auto...) {});
  MockGestureRecognizer recognizer;
  auto* member = arena.Add(&recognizer);
  arena.OnEvent(GetDefaultPointerEvent());

  EXPECT_TRUE(recognizer.OnContestStartedWasCalled());
  // Hold the arena to wait for another interaction.
  member->Hold();

  {
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::REMOVE);
    arena.OnEvent(event);
  }

  recognizer.Reset();

  arena.OnEvent(GetDefaultPointerEvent());
  // Arena is held, so the contending is not finished yet.
  EXPECT_FALSE(recognizer.OnContestStartedWasCalled());
}

TEST_F(GestureArenaTest, ArenaSweeps) {
  GestureArena arena([](auto...) {});
  MockGestureRecognizer recognizer_1;
  MockGestureRecognizer recognizer_2;
  arena.Add(&recognizer_1);
  arena.Add(&recognizer_2);
  arena.OnEvent(GetDefaultPointerEvent());

  // Both are still contending at this point.
  EXPECT_FALSE(recognizer_1.OnWinWasCalled());
  EXPECT_FALSE(recognizer_2.OnDefeatWasCalled());

  {
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::REMOVE);
    arena.OnEvent(event);
  }

  // The interaction has ended and there is no winner. Sweeps the arena.
  EXPECT_TRUE(recognizer_1.OnWinWasCalled());
  EXPECT_TRUE(recognizer_2.OnDefeatWasCalled());
}

}  // namespace
}  // namespace a11y
