// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/arena_v2/gesture_arena_v2.h"

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>

#include <array>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "src/ui/a11y/lib/gesture_manager/arena_v2/contest_member_v2.h"
#include "src/ui/a11y/lib/gesture_manager/arena_v2/recognizer_v2.h"
#include "src/ui/a11y/lib/testing/input.h"

namespace accessibility_test {
namespace {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;

class MockGestureRecognizer : public a11y::GestureRecognizerV2 {
 public:
  MockGestureRecognizer() = default;
  ~MockGestureRecognizer() = default;

  void OnWin() override {
    won_ = true;
    if (on_win_) {
      on_win_();
    }
  }
  void SetOnWin(fit::closure on_win) { on_win_ = std::move(on_win); }
  bool OnWinWasCalled() { return won_; }

  void OnDefeat() override {
    lost_ = true;
    if (on_defeat_) {
      on_defeat_();
    }
  }
  void SetOnDefeat(fit::closure on_defeat) { on_defeat_ = std::move(on_defeat); }
  bool OnDefeatWasCalled() { return lost_; }

  void HandleEvent(const AccessibilityPointerEvent& pointer_event) override {
    num_events_++;
    if (handle_event_) {
      handle_event_(pointer_event);
    }
  }
  void SetHandleEvent(fit::function<void(const AccessibilityPointerEvent&)> handle_event) {
    handle_event_ = std::move(handle_event);
  }

  uint32_t num_events() { return num_events_; }

  void OnContestStarted(std::unique_ptr<a11y::ContestMemberV2> contest_member) override {
    contest_member_ = std::move(contest_member);
  }

  const a11y::ContestMemberV2* contest_member() const { return contest_member_.get(); }
  std::unique_ptr<a11y::ContestMemberV2>& contest_member() { return contest_member_; }

  std::string DebugName() const override { return "mock_gesture_recognizer"; }

  void Reset() {
    won_ = false;
    lost_ = false;
    num_events_ = 0;
    contest_member_.reset();
  }

 private:
  bool won_ = false;
  bool lost_ = false;
  uint32_t num_events_ = 0;

  fit::closure on_win_, on_defeat_;
  fit::function<void(const AccessibilityPointerEvent&)> handle_event_;

  std::unique_ptr<a11y::ContestMemberV2> contest_member_;
};

constexpr uint32_t kDefaultDeviceID = 42;

void SendPointerEvent(a11y::GestureArenaV2* arena, const PointerParams& event) {
  AccessibilityPointerEvent pointer_event = ToPointerEvent(event, 0);
  pointer_event.set_device_id(kDefaultDeviceID);
  arena->OnEvent(pointer_event);
}

void SendPointerEvents(a11y::GestureArenaV2* arena, const std::vector<PointerParams>& events) {
  for (const auto& event : events) {
    SendPointerEvent(arena, event);
  }
}

TEST(GestureArenaTest, NoContestAtStart) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);

  EXPECT_FALSE(recognizer.contest_member());
}

TEST(GestureArenaTest, ContendingOnAddEvent) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);

  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  EXPECT_TRUE(recognizer.contest_member());
  EXPECT_FALSE(recognizer.OnWinWasCalled());
  EXPECT_FALSE(recognizer.OnDefeatWasCalled());
}

TEST(GestureArenaTest, AcceptWins) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);

  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  recognizer.contest_member()->Accept();
  EXPECT_TRUE(recognizer.OnWinWasCalled());
  EXPECT_FALSE(recognizer.OnDefeatWasCalled());
}

TEST(GestureArenaTest, RejectLoses) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);

  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  recognizer.contest_member()->Reject();
  EXPECT_FALSE(recognizer.OnWinWasCalled());
  EXPECT_TRUE(recognizer.OnDefeatWasCalled());
}

TEST(GestureArenaTest, ResolveAfterAllDecided) {
  a11y::GestureArenaV2 arena;
  std::array<MockGestureRecognizer, 2> recognizers;
  arena.Add(&recognizers[0]);
  arena.Add(&recognizers[1]);

  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  recognizers[0].contest_member()->Accept();
  EXPECT_FALSE(recognizers[0].OnWinWasCalled());
  recognizers[1].contest_member()->Reject();
  EXPECT_TRUE(recognizers[0].OnWinWasCalled());
}

// Ensures the highest priority |Accept| gets the win.
TEST(GestureArenaTest, HighestPriorityAccept) {
  a11y::GestureArenaV2 arena;
  std::array<MockGestureRecognizer, 3> recognizers;
  arena.Add(&recognizers[0]);
  arena.Add(&recognizers[1]);
  arena.Add(&recognizers[2]);

  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  recognizers[1].contest_member()->Accept();
  recognizers[0].contest_member()->Accept();
  recognizers[2].contest_member()->Accept();

  EXPECT_TRUE(recognizers[0].OnWinWasCalled());
  EXPECT_FALSE(recognizers[0].OnDefeatWasCalled());
  EXPECT_FALSE(recognizers[1].OnWinWasCalled());
  EXPECT_TRUE(recognizers[1].OnDefeatWasCalled());
  EXPECT_FALSE(recognizers[2].OnWinWasCalled());
  EXPECT_TRUE(recognizers[2].OnDefeatWasCalled());
}

TEST(GestureArenaTest, ReleaseRejectsByDefault) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);

  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  recognizer.contest_member().reset();
  EXPECT_FALSE(recognizer.OnWinWasCalled());
  EXPECT_TRUE(recognizer.OnDefeatWasCalled());
}

// Ensures that if a member is released after calling |Accept|, it can still receive a win.
TEST(GestureArenaTest, ReleasedCanWin) {
  a11y::GestureArenaV2 arena;
  std::array<MockGestureRecognizer, 2> recognizers;
  arena.Add(&recognizers[0]);
  arena.Add(&recognizers[1]);

  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  recognizers[0].contest_member()->Accept();
  recognizers[0].contest_member().reset();
  recognizers[1].contest_member()->Reject();

  EXPECT_TRUE(recognizers[0].OnWinWasCalled());
  EXPECT_FALSE(recognizers[0].OnDefeatWasCalled());
}

// This test makes sure that pointer events are sent to all active arena members, either because
// they are still contending or they haven't released yet.
TEST(GestureArenaTest, RoutePointerEvents) {
  std::optional<uint32_t> actual_device_id;
  std::optional<uint32_t> actual_pointer_id;
  std::optional<fuchsia::ui::input::accessibility::EventHandling> actual_handled;
  a11y::GestureArenaV2 arena([&actual_device_id, &actual_pointer_id, &actual_handled](
                                 uint32_t device_id, uint32_t pointer_id,
                                 fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id = device_id;
    actual_pointer_id = pointer_id;
    actual_handled = handled;
  });
  std::array<MockGestureRecognizer, 2> recognizers;
  arena.Add(&recognizers[0]);
  arena.Add(&recognizers[1]);

  // ADD event, will have a callback later indicating weather the pointer event stream was consumed
  // or rejected.
  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  EXPECT_EQ(recognizers[0].num_events(), 1u);
  EXPECT_EQ(recognizers[1].num_events(), 1u);

  EXPECT_FALSE(actual_handled) << "Arena should not prematurely notify that events were consumed.";
  recognizers[0].contest_member()->Accept();
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_EQ(actual_device_id, kDefaultDeviceID);
  EXPECT_EQ(actual_pointer_id, 1);

  // DOWN event, will not have the callback invoked.
  SendPointerEvent(&arena, {1, Phase::DOWN, {}});

  EXPECT_EQ(recognizers[0].num_events(), 2u);
  EXPECT_EQ(recognizers[1].num_events(), 2u);

  recognizers[1].contest_member()->Reject();
  SendPointerEvent(&arena, {1, Phase::UP, {}});

  EXPECT_EQ(recognizers[0].num_events(), 3u);
  // Recognizer 1 has been defeated, so it should no longer receive events.

  recognizers[0].contest_member().reset();
  SendPointerEvent(&arena, {1, Phase::REMOVE, {}});

  // Recognizer 0 has been released, so it should no longer receive events.
  EXPECT_EQ(recognizers[0].num_events(), 3u);
  EXPECT_EQ(recognizers[1].num_events(), 2u);
}

// This test makes sure that when all members reject, the input system is notified of the rejection.
TEST(GestureArenaTest, EmptyArenaRejectsPointerEvents) {
  std::optional<uint32_t> actual_device_id;
  std::optional<uint32_t> actual_pointer_id;
  std::optional<fuchsia::ui::input::accessibility::EventHandling> actual_handled;
  a11y::GestureArenaV2 arena([&actual_device_id, &actual_pointer_id, &actual_handled](
                                 uint32_t device_id, uint32_t pointer_id,
                                 fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id = device_id;
    actual_pointer_id = pointer_id;
    actual_handled = handled;
  });
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);

  SendPointerEvent(&arena, {1, Phase::ADD, {}});
  recognizer.contest_member()->Reject();

  // The input system should see the callback now, as all members have rejected.
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::REJECTED);
  EXPECT_EQ(actual_device_id, kDefaultDeviceID);
  EXPECT_EQ(actual_pointer_id, 1);
}

TEST(GestureArenaTest, HoldUnresolvedArena) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);
  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  // Hold the arena to wait for another interaction. Move it into a local so we can verify that a
  // new one wasn't vended.
  std::unique_ptr<a11y::ContestMemberV2> first_member = std::move(recognizer.contest_member());

  SendPointerEvent(&arena, {1, Phase::REMOVE, {}});
  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  // Arena is held, so the contest is not finished yet.
  EXPECT_FALSE(recognizer.contest_member());
}

TEST(GestureArenaTest, HoldResolvedArena) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);
  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  recognizer.contest_member()->Accept();

  // Hold the arena to wait for another interaction. Move it into a local so we can verify that a
  // new one wasn't vended.
  std::unique_ptr<a11y::ContestMemberV2> first_member = std::move(recognizer.contest_member());

  SendPointerEvent(&arena, {1, Phase::REMOVE, {}});
  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  // Arena is held, so the contest is not finished yet.
  EXPECT_FALSE(recognizer.contest_member());
}

// Ensures that a recognizer need not resolve while an interaction is still in progress to route
// status properly.
TEST(GestureArenaTest, ConsumeAfterInteraction) {
  std::optional<uint32_t> actual_device_id;
  std::optional<uint32_t> actual_pointer_id;
  std::optional<fuchsia::ui::input::accessibility::EventHandling> actual_handled;
  a11y::GestureArenaV2 arena([&actual_device_id, &actual_pointer_id, &actual_handled](
                                 uint32_t device_id, uint32_t pointer_id,
                                 fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id = device_id;
    actual_pointer_id = pointer_id;
    actual_handled = handled;
  });
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);

  SendPointerEvents(&arena, TapEvents(1, {}));
  recognizer.contest_member()->Accept();

  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_EQ(actual_device_id, kDefaultDeviceID);
  EXPECT_EQ(actual_pointer_id, 1);
}

// Ensures that while a consuming arena is held, subsequent streams are consumed as well.
TEST(GestureArenaTest, ConsumeSubsequentStreams) {
  std::optional<uint32_t> actual_device_id;
  std::optional<uint32_t> actual_pointer_id;
  std::optional<fuchsia::ui::input::accessibility::EventHandling> actual_handled;
  a11y::GestureArenaV2 arena([&actual_device_id, &actual_pointer_id, &actual_handled](
                                 uint32_t device_id, uint32_t pointer_id,
                                 fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id = device_id;
    actual_pointer_id = pointer_id;
    actual_handled = handled;
  });
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);

  SendPointerEvents(&arena, TapEvents(1, {}));
  recognizer.contest_member()->Accept();

  actual_device_id.reset();
  actual_pointer_id.reset();
  actual_handled.reset();

  SendPointerEvents(&arena, TapEvents(1, {}));

  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_EQ(actual_device_id, kDefaultDeviceID);
  EXPECT_EQ(actual_pointer_id, 1);
}

TEST(GestureArenaTest, NewContest) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);

  SendPointerEvents(&arena, TapEvents(1, {}));
  recognizer.contest_member().reset();

  SendPointerEvent(&arena, {1, Phase::ADD, {}});
  EXPECT_TRUE(recognizer.contest_member());
}

// Exercises |ContestMemberV2| release during |OnWin| as a result of |Accept()|.
TEST(GestureArenaTest, PoisonAcceptWin) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);
  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  recognizer.SetOnWin([&] { recognizer.contest_member().reset(); });
  recognizer.contest_member()->Accept();

  FX_CHECK(!recognizer.contest_member());
}

// Exercises |ContestMemberV2| release during |OnDefeat| as a result of |Accept()|.
TEST(GestureArenaTest, PoisonAcceptDefeat) {
  a11y::GestureArenaV2 arena;
  std::array<MockGestureRecognizer, 2> recognizers;
  arena.Add(&recognizers[0]);
  arena.Add(&recognizers[1]);
  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  recognizers[1].SetOnDefeat([&] { recognizers[1].contest_member().reset(); });
  recognizers[0].contest_member()->Accept();
  recognizers[1].contest_member()->Accept();

  FX_CHECK(!recognizers[1].contest_member());
}

// Exercises |ContestMemberV2| release during |OnDefeat| as a result of |Reject()|.
TEST(GestureArenaTest, PoisonReject) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);
  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  recognizer.SetOnDefeat([&] { recognizer.contest_member().reset(); });
  recognizer.contest_member()->Reject();

  FX_CHECK(!recognizer.contest_member());
}

// Exercises |ContestMemberV2| release during |HandleEvent| while still contending.
TEST(GestureArenaTest, PoisonContendingEvent) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);
  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  recognizer.SetHandleEvent([&](const auto&) { recognizer.contest_member().reset(); });
  SendPointerEvent(&arena, {1, Phase::DOWN, {}});

  FX_CHECK(!recognizer.contest_member());
}

// Exercises |ContestMemberV2| release during |HandleEvent| after winning.
TEST(GestureArenaTest, PoisonWinnerEvent) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);
  SendPointerEvent(&arena, {1, Phase::ADD, {}});
  recognizer.contest_member()->Accept();

  recognizer.SetHandleEvent([&](const auto&) { recognizer.contest_member().reset(); });
  SendPointerEvent(&arena, {1, Phase::DOWN, {}});

  FX_CHECK(!recognizer.contest_member());
}

}  // namespace
}  // namespace accessibility_test
