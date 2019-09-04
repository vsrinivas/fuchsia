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

  std::string DebugName() override { return "mock_gesture_recognizer"; }

 private:
  bool won_ = false;
  bool lost_ = false;
  uint32_t num_events_ = 0;
};

using GestureArenaTest = gtest::TestLoopFixture;

// Returns a default Accessibility Pointer Event.
AccessibilityPointerEvent GetDefaultPointerEvent() {
  AccessibilityPointerEvent event;
  event.set_event_time(10);
  event.set_device_id(1);
  event.set_pointer_id(1);
  event.set_type(fuchsia::ui::input::PointerEventType::TOUCH);
  event.set_phase(Phase::ADD);
  event.set_global_point({4, 4});
  event.set_viewref_koid(100);
  event.set_local_point({2, 2});
  return event;
}

TEST_F(GestureArenaTest, SingleContenderWins) {
  GestureArena arena;
  MockGestureRecognizer recognizer;
  auto* member = arena.Add(&recognizer);
  auto event = GetDefaultPointerEvent();
  arena.OnEvent(std::move(event), [](auto...) {});
  EXPECT_EQ(member->status(), ArenaMember::kWinner);
}

TEST_F(GestureArenaTest, FirstContenderClaimVictoryWins) {
  GestureArena arena;
  MockGestureRecognizer recognizer_1;
  MockGestureRecognizer recognizer_2;
  auto* member_1 = arena.Add(&recognizer_1);
  auto* member_2 = arena.Add(&recognizer_2);
  {
    auto event = GetDefaultPointerEvent();
    arena.OnEvent(std::move(event), [](auto...) {});
  }

  EXPECT_EQ(member_1->status(), ArenaMember::kContending);
  EXPECT_EQ(member_2->status(), ArenaMember::kContending);

  {
    auto event = GetDefaultPointerEvent();
    arena.OnEvent(std::move(event), [](auto...) {});
  }
  EXPECT_TRUE(member_1->ClaimWin());
  EXPECT_TRUE(recognizer_1.OnWinWasCalled());
  EXPECT_TRUE(recognizer_2.OnDefeatWasCalled());
  EXPECT_EQ(member_1->status(), ArenaMember::kWinner);
  EXPECT_EQ(member_2->status(), ArenaMember::kDefeated);
}

TEST_F(GestureArenaTest, SecondContenderClaimVictoryFails) {
  GestureArena arena;
  MockGestureRecognizer recognizer_1;
  MockGestureRecognizer recognizer_2;
  auto* member_1 = arena.Add(&recognizer_1);
  auto* member_2 = arena.Add(&recognizer_2);

  {
    auto event = GetDefaultPointerEvent();
    arena.OnEvent(std::move(event), [](auto...) {});
  }

  EXPECT_EQ(member_1->status(), ArenaMember::kContending);
  EXPECT_EQ(member_2->status(), ArenaMember::kContending);

  EXPECT_TRUE(member_1->ClaimWin());
  EXPECT_FALSE(member_2->ClaimWin());
  EXPECT_TRUE(recognizer_1.OnWinWasCalled());
  EXPECT_TRUE(recognizer_2.OnDefeatWasCalled());
  EXPECT_EQ(member_1->status(), ArenaMember::kWinner);
  EXPECT_EQ(member_2->status(), ArenaMember::kDefeated);
}

TEST_F(GestureArenaTest, LastStandingWins) {
  GestureArena arena;
  MockGestureRecognizer recognizer_1;
  MockGestureRecognizer recognizer_2;
  MockGestureRecognizer recognizer_3;
  auto* member_1 = arena.Add(&recognizer_1);
  auto* member_2 = arena.Add(&recognizer_2);
  auto* member_3 = arena.Add(&recognizer_3);

  {
    auto event = GetDefaultPointerEvent();
    arena.OnEvent(std::move(event), [](auto...) {});
  }

  EXPECT_EQ(member_1->status(), ArenaMember::kContending);
  EXPECT_EQ(member_2->status(), ArenaMember::kContending);
  EXPECT_EQ(member_3->status(), ArenaMember::kContending);

  EXPECT_TRUE(member_1->DeclareDefeat());
  EXPECT_TRUE(recognizer_1.OnDefeatWasCalled());

  EXPECT_EQ(member_1->status(), ArenaMember::kDefeated);
  EXPECT_EQ(member_2->status(), ArenaMember::kContending);
  EXPECT_EQ(member_3->status(), ArenaMember::kContending);

  EXPECT_TRUE(member_3->DeclareDefeat());
  EXPECT_TRUE(recognizer_3.OnDefeatWasCalled());
  EXPECT_TRUE(recognizer_2.OnWinWasCalled());
  EXPECT_EQ(member_2->status(), ArenaMember::kWinner);
  EXPECT_EQ(member_3->status(), ArenaMember::kDefeated);
}

TEST_F(GestureArenaTest, RoutPointerEvents) {
  // This test makes sure that pointer events are sent to all active arena
  // members, either because they are still contending or they already won the
  // gesture. In the end, when the recognizer signs the end of processing for
  // that gesture, it also tests that the pointer event callbacks are called.
  GestureArena arena;
  MockGestureRecognizer recognizer_1;
  MockGestureRecognizer recognizer_2;
  auto* member_1 = arena.Add(&recognizer_1);
  auto* member_2 = arena.Add(&recognizer_2);

  bool event_1_called = false;
  fuchsia::ui::input::accessibility::EventHandling actual_handled_1 =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;
  {
    // ADD event, will have a callback later indicating weather the pointer event stream was
    // consumed or rejected.
    auto event = GetDefaultPointerEvent();
    arena.OnEvent(std::move(event), [&event_1_called, &actual_handled_1](
                                        uint32_t device_id, uint32_t pointer_id,
                                        fuchsia::ui::input::accessibility::EventHandling handled) {
      event_1_called = true;
      actual_handled_1 = handled;
    });
  }

  EXPECT_EQ(member_1->status(), ArenaMember::kContending);
  EXPECT_EQ(member_2->status(), ArenaMember::kContending);
  EXPECT_EQ(recognizer_1.num_events(), 1u);
  EXPECT_EQ(recognizer_2.num_events(), 1u);

  bool event_2_called = false;
  {
    // DOWN event, will  not have the callback invoked.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    arena.OnEvent(std::move(event), [&event_2_called](auto...) { event_2_called = true; });
  }

  EXPECT_EQ(recognizer_1.num_events(), 2u);
  EXPECT_EQ(recognizer_2.num_events(), 2u);

  EXPECT_TRUE(member_1->ClaimWin());
  EXPECT_TRUE(recognizer_1.OnWinWasCalled());
  EXPECT_TRUE(recognizer_2.OnDefeatWasCalled());
  EXPECT_EQ(member_1->status(), ArenaMember::kWinner);
  EXPECT_EQ(member_2->status(), ArenaMember::kDefeated);

  bool event_3_called = false;
  fuchsia::ui::input::accessibility::EventHandling actual_handled_3 =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;
  {
    auto event = GetDefaultPointerEvent();
    arena.OnEvent(std::move(event), [&event_3_called, &actual_handled_3](
                                        uint32_t device_id, uint32_t pointer_id,
                                        fuchsia::ui::input::accessibility::EventHandling handled) {
      event_3_called = true;
      actual_handled_3 = handled;
    });
  }

  EXPECT_EQ(recognizer_1.num_events(), 3u);
  EXPECT_EQ(recognizer_2.num_events(), 2u);

  member_1->StopRoutingPointerEvents(fuchsia::ui::input::accessibility::EventHandling::CONSUMED);

  EXPECT_EQ(actual_handled_1, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_EQ(actual_handled_3, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);

  EXPECT_TRUE(event_1_called);
  EXPECT_FALSE(event_2_called);
  EXPECT_TRUE(event_3_called);
}

}  // namespace
}  // namespace a11y
