// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/arena/gesture_arena.h"

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/fit/function.h>

#include <array>
#include <memory>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/gesture_manager/arena/contest_member.h"
#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"
#include "src/ui/a11y/lib/testing/input.h"

namespace accessibility_test {
namespace {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;

class MockGestureRecognizer : public a11y::GestureRecognizer {
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

  void OnContestStarted(std::unique_ptr<a11y::ContestMember> contest_member) override {
    contest_member_ = std::move(contest_member);
  }

  const a11y::ContestMember* contest_member() const { return contest_member_.get(); }
  std::unique_ptr<a11y::ContestMember>& contest_member() { return contest_member_; }

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

  std::unique_ptr<a11y::ContestMember> contest_member_;
};

constexpr uint32_t kDefaultDeviceID = 42;

auto MemberStatusEq(a11y::ContestMember::Status status) {
  return testing::Property(&MockGestureRecognizer::contest_member,
                           testing::Property(&a11y::ContestMember::status, status));
}

void SendPointerEvent(a11y::GestureArena* arena, const PointerParams& event) {
  AccessibilityPointerEvent pointer_event = ToPointerEvent(event, 0);
  pointer_event.set_device_id(kDefaultDeviceID);
  arena->OnEvent(pointer_event);
}

void SendPointerEvents(a11y::GestureArena* arena, const std::vector<PointerParams>& events) {
  for (const auto& event : events) {
    SendPointerEvent(arena, event);
  }
}

TEST(GestureArenaTest, SingleContenderWins) {
  a11y::GestureArena arena([](auto...) {});
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);

  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  EXPECT_THAT(recognizer, MemberStatusEq(a11y::ContestMember::Status::kWinner));
}

TEST(GestureArenaTest, AllMembersAreContendingOnAddEvent) {
  a11y::GestureArena arena([](auto...) {});
  std::array<MockGestureRecognizer, 3> recognizers;
  arena.Add(&recognizers[0]);
  arena.Add(&recognizers[1]);
  arena.Add(&recognizers[2]);

  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  EXPECT_THAT(recognizers, testing::Each(MemberStatusEq(a11y::ContestMember::Status::kContending)));
}

TEST(GestureArenaTest, FirstContenderClaimVictoryWins) {
  a11y::GestureArena arena([](auto...) {});
  std::array<MockGestureRecognizer, 2> recognizers;
  arena.Add(&recognizers[0]);
  arena.Add(&recognizers[1]);

  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  EXPECT_TRUE(recognizers[0].contest_member()->Accept());
  EXPECT_TRUE(recognizers[0].OnWinWasCalled());
  EXPECT_TRUE(recognizers[1].OnDefeatWasCalled());
  EXPECT_THAT(recognizers[0], MemberStatusEq(a11y::ContestMember::Status::kWinner));
  EXPECT_THAT(recognizers[1], MemberStatusEq(a11y::ContestMember::Status::kDefeated));
}

TEST(GestureArenaTest, SecondContenderClaimVictoryFails) {
  a11y::GestureArena arena([](auto...) {});
  std::array<MockGestureRecognizer, 2> recognizers;
  arena.Add(&recognizers[0]);
  arena.Add(&recognizers[1]);

  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  EXPECT_TRUE(recognizers[0].contest_member()->Accept());
  EXPECT_FALSE(recognizers[1].contest_member()->Accept());
  EXPECT_TRUE(recognizers[0].OnWinWasCalled());
  EXPECT_TRUE(recognizers[1].OnDefeatWasCalled());
  EXPECT_THAT(recognizers[0], MemberStatusEq(a11y::ContestMember::Status::kWinner));
  EXPECT_THAT(recognizers[1], MemberStatusEq(a11y::ContestMember::Status::kDefeated));
}

TEST(GestureArenaTest, LastStandingWins) {
  a11y::GestureArena arena([](auto...) {});
  std::array<MockGestureRecognizer, 3> recognizers;
  arena.Add(&recognizers[0]);
  arena.Add(&recognizers[1]);
  arena.Add(&recognizers[2]);

  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  recognizers[0].contest_member()->Reject();
  EXPECT_TRUE(recognizers[0].OnDefeatWasCalled());

  EXPECT_THAT(recognizers[0], MemberStatusEq(a11y::ContestMember::Status::kDefeated));
  EXPECT_THAT(recognizers[1], MemberStatusEq(a11y::ContestMember::Status::kContending));
  EXPECT_THAT(recognizers[2], MemberStatusEq(a11y::ContestMember::Status::kContending));

  recognizers[2].contest_member()->Reject();
  EXPECT_TRUE(recognizers[2].OnDefeatWasCalled());
  EXPECT_TRUE(recognizers[1].OnWinWasCalled());
  EXPECT_THAT(recognizers[1], MemberStatusEq(a11y::ContestMember::Status::kWinner));
  EXPECT_THAT(recognizers[2], MemberStatusEq(a11y::ContestMember::Status::kDefeated));
}

// This test makes sure that pointer events are sent to all active arena members, either because
// they are still contending or they haven't called Reject() yet.
TEST(GestureArenaTest, RoutePointerEvents) {
  std::optional<uint32_t> actual_device_id;
  std::optional<uint32_t> actual_pointer_id;
  std::optional<fuchsia::ui::input::accessibility::EventHandling> actual_handled;
  a11y::GestureArena arena([&actual_device_id, &actual_pointer_id, &actual_handled](
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

  // DOWN event, will not have the callback invoked.
  SendPointerEvent(&arena, {1, Phase::DOWN, {}});

  EXPECT_EQ(recognizers[0].num_events(), 2u);
  EXPECT_EQ(recognizers[1].num_events(), 2u);

  EXPECT_TRUE(recognizers[0].contest_member()->Accept());

  SendPointerEvent(&arena, {1, Phase::UP, {}});

  EXPECT_EQ(recognizers[0].num_events(), 3u);
  // recognizer 1 has been defeated, so it should no longer receive events.

  SendPointerEvent(&arena, {1, Phase::REMOVE, {}});

  EXPECT_EQ(recognizers[0].num_events(), 4u);
  EXPECT_EQ(recognizers[1].num_events(), 2u);

  // TODO(rosswang): We may be able to drop this in the future if we allow soft, terminal |Accept|s
  // and no default winners.
  EXPECT_FALSE(actual_handled) << "Arena should not prematurely notify that events were consumed "
                                  "when the winner is still active.";

  recognizers[0].contest_member().reset();

  // The interaction ended, check callbacks.
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_EQ(actual_device_id, kDefaultDeviceID);
  EXPECT_EQ(actual_pointer_id, 1);
}

// This test makes sure that when the arena is empty and configured to consume pointer events, the
// input system gets the appropriate callback.
TEST(GestureArenaTest, EmptyArenaConsumesPointerEvents) {
  std::optional<uint32_t> actual_device_id;
  std::optional<uint32_t> actual_pointer_id;
  std::optional<fuchsia::ui::input::accessibility::EventHandling> actual_handled;
  a11y::GestureArena arena(
      [&actual_device_id, &actual_pointer_id, &actual_handled](
          uint32_t device_id, uint32_t pointer_id,
          fuchsia::ui::input::accessibility::EventHandling handled) {
        actual_device_id = device_id;
        actual_pointer_id = pointer_id;
        actual_handled = handled;
      },
      a11y::GestureArena::EventHandlingPolicy::kConsumeEvents);
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);

  // ADD event, will have a callback later indicating weather the pointer event stream was consumed
  // or rejected.
  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  // Single contender always wins.
  recognizer.contest_member()->Reject();  // The arena becomes empty.

  // The input system should see the callback now, as the arena is empty.
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_EQ(actual_device_id, kDefaultDeviceID);
  EXPECT_EQ(actual_pointer_id, 1);

  // Continue with the sequence of events, until the interaction is over.
  SendPointerEvent(&arena, {1, Phase::DOWN, {}});
  SendPointerEvents(&arena, UpEvents(1, {}));

  // Although the system forwards us the rest of the events, our recognizer should have surrendered
  // them.
  EXPECT_EQ(recognizer.num_events(), 1u);
}

TEST(GestureArenaTest, EmptyArenaRejectsPointerEvents) {
  // This test makes sure that when the arena is empty and configured to reject pointer events, the
  // input system gets the appropriate callback.
  std::optional<fuchsia::ui::input::accessibility::EventHandling> actual_handled;
  a11y::GestureArena arena(
      [&actual_handled](uint32_t device_id, uint32_t pointer_id,
                        fuchsia::ui::input::accessibility::EventHandling handled) {
        actual_handled = handled;
      },
      a11y::GestureArena::EventHandlingPolicy::kRejectEvents);
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);

  // ADD event, will have a callback later indicating weather the pointer event stream was consumed
  // or rejected.
  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  // Single contender always wins.
  recognizer.contest_member()->Reject();  // The arena becomes empty.

  // The input system should see the callback now, as the arena is empty.
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::REJECTED);

  EXPECT_EQ(recognizer.num_events(), 1u);
  // Unlike the test above, input system does not send more events to us, so the interaction is
  // over.
}

TEST(GestureArenaTest, DoNotCallOnContendingStartedWhenArenaIsHeld) {
  a11y::GestureArena arena([](auto...) {});
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);
  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  EXPECT_TRUE(recognizer.contest_member());
  // Hold the arena to wait for another interaction. Move it into a local so we can verify that a
  // new one wasn't vended.
  std::unique_ptr<a11y::ContestMember> first_member = std::move(recognizer.contest_member());

  SendPointerEvent(&arena, {1, Phase::REMOVE, {}});
  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  // Arena is held, so the contest is not finished yet.
  EXPECT_FALSE(recognizer.contest_member());
}

TEST(GestureArenaTest, ArenaSweeps) {
  a11y::GestureArena arena([](auto...) {});
  std::array<MockGestureRecognizer, 2> recognizers;
  arena.Add(&recognizers[0]);
  arena.Add(&recognizers[1]);
  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  // Make them both passive.
  recognizers[0].contest_member().reset();
  recognizers[1].contest_member().reset();

  // Both are still contending at this point.
  EXPECT_FALSE(recognizers[0].OnWinWasCalled());
  EXPECT_FALSE(recognizers[1].OnDefeatWasCalled());

  SendPointerEvent(&arena, {1, Phase::REMOVE, {}});

  // The interaction has ended and there is no winner. Sweeps the arena.
  EXPECT_TRUE(recognizers[0].OnWinWasCalled());
  EXPECT_TRUE(recognizers[1].OnDefeatWasCalled());
}

// Exercises |ContestMember| release during |OnWin| and |OnDefeat| as a result of
// |ContestMember::Accept()|.
TEST(GestureArenaTest, PoisonAccept) {
  a11y::GestureArena arena([](auto...) {});
  std::array<MockGestureRecognizer, 2> recognizers;
  arena.Add(&recognizers[0]);
  arena.Add(&recognizers[1]);
  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  recognizers[0].SetOnWin([&] { recognizers[0].contest_member().reset(); });
  recognizers[1].SetOnDefeat([&] { recognizers[1].contest_member().reset(); });
  recognizers[0].contest_member()->Accept();

  FX_CHECK(!recognizers[0].contest_member());
  FX_CHECK(!recognizers[1].contest_member());
}

// Exercises |ContestMember| release during |OnDefeat| and |OnWin| as a result of
// |ContestMember::Reject()|.
TEST(GestureArenaTest, PoisonReject) {
  a11y::GestureArena arena([](auto...) {});
  std::array<MockGestureRecognizer, 2> recognizers;
  arena.Add(&recognizers[0]);
  arena.Add(&recognizers[1]);
  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  recognizers[0].SetOnDefeat([&] { recognizers[0].contest_member().reset(); });
  recognizers[1].SetOnWin([&] { recognizers[1].contest_member().reset(); });
  recognizers[0].contest_member()->Reject();

  FX_CHECK(!recognizers[0].contest_member());
  FX_CHECK(!recognizers[1].contest_member());
}

// Exercises |ContestMember| release during |HandleEvent| while still contending.
TEST(GestureArenaTest, PoisonContendingEvent) {
  a11y::GestureArena arena([](auto...) {});
  std::array<MockGestureRecognizer, 2> recognizers;
  arena.Add(&recognizers[0]);
  arena.Add(&recognizers[1]);
  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  recognizers[0].SetHandleEvent([&](const auto&) { recognizers[0].contest_member().reset(); });
  SendPointerEvent(&arena, {1, Phase::DOWN, {}});

  FX_CHECK(!recognizers[0].contest_member());
}

// Exercises |ContestMember| release during |HandleEvent| after winning by default.
TEST(GestureArenaTest, PoisonWinnerEvent) {
  a11y::GestureArena arena([](auto...) {});
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);
  SendPointerEvent(&arena, {1, Phase::ADD, {}});

  recognizer.SetHandleEvent([&](const auto&) { recognizer.contest_member().reset(); });
  SendPointerEvent(&arena, {1, Phase::DOWN, {}});

  FX_CHECK(!recognizer.contest_member());
}

}  // namespace
}  // namespace accessibility_test
