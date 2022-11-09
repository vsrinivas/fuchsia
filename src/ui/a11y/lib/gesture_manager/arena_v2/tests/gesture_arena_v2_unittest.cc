// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/arena_v2/gesture_arena_v2.h"

#include <fuchsia/ui/pointer/augment/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "fuchsia/ui/input/cpp/fidl.h"
#include "fuchsia/ui/pointer/cpp/fidl.h"
#include "src/ui/a11y/lib/gesture_manager/arena_v2/participation_token_interface.h"
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

  void HandleEvent(const fuchsia::ui::pointer::augment::TouchEventWithLocalHit& event) override {
    num_events_++;
    if (handle_event_) {
      handle_event_(event);
    }
  }
  void SetHandleEvent(
      fit::function<void(const fuchsia::ui::pointer::augment::TouchEventWithLocalHit&)>
          handle_event) {
    handle_event_ = std::move(handle_event);
  }

  uint32_t num_events() { return num_events_; }

  void OnContestStarted(
      std::unique_ptr<a11y::ParticipationTokenInterface> participation_token) override {
    participation_token_ = std::move(participation_token);
  }

  const a11y::ParticipationTokenInterface* participation_token() const {
    return participation_token_.get();
  }
  std::unique_ptr<a11y::ParticipationTokenInterface>& participation_token() {
    return participation_token_;
  }

  std::string DebugName() const override { return "mock_gesture_recognizer"; }

  void Reset() {
    won_ = false;
    lost_ = false;
    num_events_ = 0;
    participation_token_.reset();
  }

 private:
  bool won_ = false;
  bool lost_ = false;
  uint32_t num_events_ = 0;

  fit::closure on_win_, on_defeat_;
  fit::function<void(const fuchsia::ui::pointer::augment::TouchEventWithLocalHit&)> handle_event_;

  std::unique_ptr<a11y::ParticipationTokenInterface> participation_token_;
};

constexpr uint32_t kDefaultDeviceID = 42;

// Convert the old `PointerEventPhase` type to the new `EventPhase` type.
fuchsia::ui::pointer::EventPhase convertPhase(fuchsia::ui::input::PointerEventPhase phase) {
  switch (phase) {
    case fuchsia::ui::input::PointerEventPhase::ADD:
      return fuchsia::ui::pointer::EventPhase::ADD;
    case fuchsia::ui::input::PointerEventPhase::MOVE:
      return fuchsia::ui::pointer::EventPhase::CHANGE;
    case fuchsia::ui::input::PointerEventPhase::REMOVE:
      return fuchsia::ui::pointer::EventPhase::REMOVE;
    case fuchsia::ui::input::PointerEventPhase::CANCEL:
      return fuchsia::ui::pointer::EventPhase::CANCEL;

    case fuchsia::ui::input::PointerEventPhase::DOWN:
    case fuchsia::ui::input::PointerEventPhase::HOVER:
    case fuchsia::ui::input::PointerEventPhase::UP:
      FX_CHECK(false) << "invalid conversion";
      return fuchsia::ui::pointer::EventPhase::CANCEL;
  }
}

fuchsia::ui::pointer::augment::TouchEventWithLocalHit touchEvent(const PointerParams& params,
                                                                 uint32_t interaction_id,
                                                                 uint64_t event_time,
                                                                 zx_koid_t koid = 0) {
  fuchsia::ui::pointer::TouchInteractionId interaction{
      .device_id = kDefaultDeviceID,
      .pointer_id = params.pointer_id,
      .interaction_id = interaction_id,
  };

  fuchsia::ui::pointer::TouchPointerSample sample;
  sample.set_interaction(interaction);
  sample.set_phase(convertPhase(params.phase));
  sample.set_position_in_viewport({params.coordinate[0], params.coordinate[1]});

  fuchsia::ui::pointer::TouchEvent inner;
  inner.set_timestamp(event_time);
  inner.set_pointer_sample(std::move(sample));

  const auto& local_point = ToLocalCoordinates(params.coordinate);
  fuchsia::ui::pointer::augment::TouchEventWithLocalHit event{
      .touch_event = std::move(inner),
      .local_viewref_koid = koid,
      .local_point = {local_point.x, local_point.y},
  };

  return event;
}

std::vector<std::pair<PointerParams, /*interaction_id=*/uint32_t>> tapEvents(
    PointerId pointer_id, const glm::vec2& coordinate, int32_t interaction_id) {
  return {
      {{pointer_id, fuchsia::ui::input::PointerEventPhase::ADD, coordinate}, interaction_id},
      {{pointer_id, fuchsia::ui::input::PointerEventPhase::REMOVE, coordinate}, interaction_id},
  };
}

a11y::InteractionTrackerV2::ConsumptionStatus SendPointerEvent(a11y::GestureArenaV2* arena,
                                                               const PointerParams& event,
                                                               uint32_t interaction_id) {
  return arena->OnEvent(touchEvent(event, interaction_id, 0));
}

void SendPointerEvents(
    a11y::GestureArenaV2* arena,
    const std::vector<std::pair<PointerParams, /*interaction_id=*/uint32_t>>& events) {
  for (const auto& [event, interaction_id] : events) {
    SendPointerEvent(arena, event, interaction_id);
  }
}

TEST(GestureArenaTest, NoContestAtStart) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);

  EXPECT_FALSE(recognizer.participation_token());
}

TEST(GestureArenaTest, ContendingOnAddEvent) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);

  SendPointerEvent(&arena, {1, Phase::ADD, {}}, 0);

  EXPECT_TRUE(recognizer.participation_token());
  EXPECT_FALSE(recognizer.OnWinWasCalled());
  EXPECT_FALSE(recognizer.OnDefeatWasCalled());
}

TEST(GestureArenaTest, AcceptWins) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);

  SendPointerEvent(&arena, {1, Phase::ADD, {}}, 0);

  recognizer.participation_token()->Accept();
  EXPECT_TRUE(recognizer.OnWinWasCalled());
  EXPECT_FALSE(recognizer.OnDefeatWasCalled());
}

TEST(GestureArenaTest, RejectLoses) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);

  SendPointerEvent(&arena, {1, Phase::ADD, {}}, 0);

  recognizer.participation_token()->Reject();
  EXPECT_FALSE(recognizer.OnWinWasCalled());
  EXPECT_TRUE(recognizer.OnDefeatWasCalled());
}

TEST(GestureArenaTest, ResolveAfterAllDecided) {
  a11y::GestureArenaV2 arena;
  std::array<MockGestureRecognizer, 2> recognizers;
  arena.Add(&recognizers[0]);
  arena.Add(&recognizers[1]);

  SendPointerEvent(&arena, {1, Phase::ADD, {}}, 0);

  recognizers[0].participation_token()->Accept();
  EXPECT_FALSE(recognizers[0].OnWinWasCalled());
  recognizers[1].participation_token()->Reject();
  EXPECT_TRUE(recognizers[0].OnWinWasCalled());
}

// Ensures the highest priority |Accept| gets the win.
TEST(GestureArenaTest, HighestPriorityAccept) {
  a11y::GestureArenaV2 arena;
  std::array<MockGestureRecognizer, 3> recognizers;
  arena.Add(&recognizers[0]);
  arena.Add(&recognizers[1]);
  arena.Add(&recognizers[2]);

  SendPointerEvent(&arena, {1, Phase::ADD, {}}, 0);

  recognizers[1].participation_token()->Accept();
  recognizers[0].participation_token()->Accept();
  recognizers[2].participation_token()->Accept();

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

  SendPointerEvent(&arena, {1, Phase::ADD, {}}, 0);

  recognizer.participation_token().reset();
  EXPECT_FALSE(recognizer.OnWinWasCalled());
  EXPECT_TRUE(recognizer.OnDefeatWasCalled());
}

// Ensures that if a token is released after calling |Accept|, it can still receive a win.
TEST(GestureArenaTest, ReleasedCanWin) {
  a11y::GestureArenaV2 arena;
  std::array<MockGestureRecognizer, 2> recognizers;
  arena.Add(&recognizers[0]);
  arena.Add(&recognizers[1]);

  SendPointerEvent(&arena, {1, Phase::ADD, {}}, 0);

  recognizers[0].participation_token()->Accept();
  recognizers[0].participation_token().reset();
  recognizers[1].participation_token()->Reject();

  EXPECT_TRUE(recognizers[0].OnWinWasCalled());
  EXPECT_FALSE(recognizers[0].OnDefeatWasCalled());
}

// This test makes sure that pointer events are sent to all participating recognizers,
// either because they are still undecided or they haven't released their token yet.
TEST(GestureArenaTest, RoutePointerEvents) {
  std::optional<fuchsia::ui::pointer::TouchInteractionId> interaction_id;
  std::optional<a11y::InteractionTrackerV2::ConsumptionStatus> consumption_status;
  auto callback = [&interaction_id, &consumption_status](
                      fuchsia::ui::pointer::TouchInteractionId id,
                      a11y::InteractionTrackerV2::ConsumptionStatus status) {
    interaction_id = id;
    consumption_status = status;
  };
  a11y::GestureArenaV2 arena(std::move(callback));
  std::array<MockGestureRecognizer, 2> recognizers;
  arena.Add(&recognizers[0]);
  arena.Add(&recognizers[1]);

  auto status = SendPointerEvent(&arena, {1, Phase::ADD, {}}, 0);
  EXPECT_EQ(status, a11y::InteractionTrackerV2::ConsumptionStatus::kUndecided);

  // Both recognizers should receive the first event.
  EXPECT_EQ(recognizers[0].num_events(), 1u);
  EXPECT_EQ(recognizers[1].num_events(), 1u);

  recognizers[0].participation_token()->Accept();
  status = arena.Status();
  EXPECT_EQ(status, a11y::InteractionTrackerV2::ConsumptionStatus::kAccept);

  recognizers[1].participation_token()->Reject();
  // Recognizer 1 has been defeated, so it should no longer receive events.

  recognizers[0].participation_token().reset();
  // Recognizer 0 has been released, so it should no longer receive events.

  status = SendPointerEvent(&arena, {1, Phase::REMOVE, {}}, 0);
  EXPECT_EQ(status, a11y::InteractionTrackerV2::ConsumptionStatus::kAccept);

  // Neither recognizer receives a second event.
  EXPECT_EQ(recognizers[0].num_events(), 1u);
  EXPECT_EQ(recognizers[1].num_events(), 1u);

  // No callback is fired, because the consumption status was decided
  // before the interaction closed.
  EXPECT_EQ(interaction_id, std::nullopt);
  EXPECT_EQ(consumption_status, std::nullopt);
}

// Similar to `RoutePointerEvents`, but additionally checking that
// a callback fires for held events.
TEST(GestureArenaTest, FireAcceptCallbackForHeldEvents) {
  std::optional<fuchsia::ui::pointer::TouchInteractionId> interaction_id;
  std::optional<a11y::InteractionTrackerV2::ConsumptionStatus> consumption_status;
  auto callback = [&interaction_id, &consumption_status](
                      fuchsia::ui::pointer::TouchInteractionId id,
                      a11y::InteractionTrackerV2::ConsumptionStatus status) {
    interaction_id = id;
    consumption_status = status;
  };
  a11y::GestureArenaV2 arena(std::move(callback));
  std::array<MockGestureRecognizer, 2> recognizers;
  arena.Add(&recognizers[0]);
  arena.Add(&recognizers[1]);

  const uint32_t id = 42;

  auto status = SendPointerEvent(&arena, {1, Phase::ADD, {}}, id);
  EXPECT_EQ(status, a11y::InteractionTrackerV2::ConsumptionStatus::kUndecided);

  EXPECT_EQ(recognizers[0].num_events(), 1u);
  EXPECT_EQ(recognizers[1].num_events(), 1u);

  status = SendPointerEvent(&arena, {1, Phase::REMOVE, {}}, id);
  EXPECT_EQ(status, a11y::InteractionTrackerV2::ConsumptionStatus::kUndecided);

  EXPECT_EQ(recognizers[0].num_events(), 2u);
  EXPECT_EQ(recognizers[1].num_events(), 2u);

  // The callback isn't fired yet, because the consumption status is
  // still undecided.
  EXPECT_EQ(interaction_id, std::nullopt);
  EXPECT_EQ(consumption_status, std::nullopt);

  recognizers[0].participation_token()->Accept();

  // Now the callback should fire, to notify that the previously "held"
  // interaction is now "accepted".
  EXPECT_EQ(interaction_id->interaction_id, id);
  EXPECT_EQ(consumption_status, a11y::InteractionTrackerV2::ConsumptionStatus::kAccept);
}

// Similar to `FireAcceptCallbackForHeldEvents`, but rejecting instead.
TEST(GestureArenaTest, FireRejectCallbackForHeldEvents) {
  std::optional<fuchsia::ui::pointer::TouchInteractionId> interaction_id;
  std::optional<a11y::InteractionTrackerV2::ConsumptionStatus> consumption_status;
  auto callback = [&interaction_id, &consumption_status](
                      fuchsia::ui::pointer::TouchInteractionId id,
                      a11y::InteractionTrackerV2::ConsumptionStatus status) {
    interaction_id = id;
    consumption_status = status;
  };
  a11y::GestureArenaV2 arena(std::move(callback));
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);

  const uint32_t id = 42;

  auto status = SendPointerEvent(&arena, {1, Phase::ADD, {}}, id);
  EXPECT_EQ(status, a11y::InteractionTrackerV2::ConsumptionStatus::kUndecided);

  status = SendPointerEvent(&arena, {1, Phase::REMOVE, {}}, id);
  EXPECT_EQ(status, a11y::InteractionTrackerV2::ConsumptionStatus::kUndecided);

  // The callback isn't fired yet, because the consumption status is
  // still undecided.
  EXPECT_EQ(interaction_id, std::nullopt);
  EXPECT_EQ(consumption_status, std::nullopt);

  recognizer.participation_token()->Reject();

  // Now the callback should fire, to notify that the previously "held"
  // interaction is now "accepted".
  EXPECT_EQ(interaction_id->interaction_id, id);
  EXPECT_EQ(consumption_status, a11y::InteractionTrackerV2::ConsumptionStatus::kReject);
}

TEST(GestureArenaTest, HoldUnresolvedArena) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);
  SendPointerEvent(&arena, {1, Phase::ADD, {}}, 0);

  // Hold the arena to wait for another interaction. Move it into a local so we can verify that a
  // new one wasn't vended.
  std::unique_ptr<a11y::ParticipationTokenInterface> first_token =
      std::move(recognizer.participation_token());

  SendPointerEvent(&arena, {1, Phase::REMOVE, {}}, 0);
  SendPointerEvent(&arena, {1, Phase::ADD, {}}, 1);

  // Arena is held, so the contest is not finished yet.
  EXPECT_FALSE(recognizer.participation_token());
}

TEST(GestureArenaTest, HoldResolvedArena) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);
  SendPointerEvent(&arena, {1, Phase::ADD, {}}, 0);

  recognizer.participation_token()->Accept();

  // Hold the arena to wait for another interaction. Move it into a local so we can verify that a
  // new one wasn't vended.
  std::unique_ptr<a11y::ParticipationTokenInterface> first_token =
      std::move(recognizer.participation_token());

  SendPointerEvent(&arena, {1, Phase::REMOVE, {}}, 0);
  SendPointerEvent(&arena, {1, Phase::ADD, {}}, 1);

  // Arena is held, so the contest is not finished yet.
  EXPECT_FALSE(recognizer.participation_token());
}

// Ensures that a recognizer need not resolve while an interaction is still in progress to route
// status properly.
TEST(GestureArenaTest, ConsumeAfterInteraction) {
  std::optional<fuchsia::ui::pointer::TouchInteractionId> actual_interaction;
  std::optional<a11y::InteractionTrackerV2::ConsumptionStatus> actual_status;
  a11y::GestureArenaV2 arena(
      [&actual_interaction, &actual_status](fuchsia::ui::pointer::TouchInteractionId interaction,
                                            a11y::InteractionTrackerV2::ConsumptionStatus status) {
        actual_interaction = interaction;
        actual_status = status;
      });
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);

  SendPointerEvents(&arena, tapEvents(1, {}, 0));
  recognizer.participation_token()->Accept();

  EXPECT_EQ(actual_status, a11y::InteractionTrackerV2::ConsumptionStatus::kAccept);
  EXPECT_EQ(actual_interaction->device_id, kDefaultDeviceID);
  EXPECT_EQ(actual_interaction->pointer_id, 1u);
}

TEST(GestureArenaTest, NewContest) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);

  SendPointerEvents(&arena, tapEvents(1, {}, 0));
  recognizer.participation_token().reset();

  SendPointerEvent(&arena, {1, Phase::ADD, {}}, 1);
  EXPECT_TRUE(recognizer.participation_token());
}

// Exercises |ParticipationToken| release during |OnWin| as a result of |Accept()|.
TEST(GestureArenaTest, PoisonAcceptWin) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);
  SendPointerEvent(&arena, {1, Phase::ADD, {}}, 0);

  recognizer.SetOnWin([&] { recognizer.participation_token().reset(); });
  recognizer.participation_token()->Accept();

  FX_CHECK(!recognizer.participation_token());
}

// Exercises |ParticipationToken| release during |OnDefeat| as a result of |Accept()|.
TEST(GestureArenaTest, PoisonAcceptDefeat) {
  a11y::GestureArenaV2 arena;
  std::array<MockGestureRecognizer, 2> recognizers;
  arena.Add(&recognizers[0]);
  arena.Add(&recognizers[1]);
  SendPointerEvent(&arena, {1, Phase::ADD, {}}, 0);

  recognizers[1].SetOnDefeat([&] { recognizers[1].participation_token().reset(); });
  recognizers[0].participation_token()->Accept();
  recognizers[1].participation_token()->Accept();

  FX_CHECK(!recognizers[1].participation_token());
}

// Exercises |ParticipationToken| release during |OnDefeat| as a result of |Reject()|.
TEST(GestureArenaTest, PoisonReject) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);
  SendPointerEvent(&arena, {1, Phase::ADD, {}}, 0);

  recognizer.SetOnDefeat([&] { recognizer.participation_token().reset(); });
  recognizer.participation_token()->Reject();

  FX_CHECK(!recognizer.participation_token());
}

// Exercises |ParticipationToken| release during |HandleEvent| while still contending.
TEST(GestureArenaTest, PoisonContendingEvent) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);
  SendPointerEvent(&arena, {1, Phase::ADD, {}}, 0);

  recognizer.SetHandleEvent([&](const auto&) { recognizer.participation_token().reset(); });
  SendPointerEvent(&arena, {1, Phase::MOVE, {}}, 0);

  FX_CHECK(!recognizer.participation_token());
}

// Exercises |ParticipationToken| release during |HandleEvent| after winning.
TEST(GestureArenaTest, PoisonWinnerEvent) {
  a11y::GestureArenaV2 arena;
  MockGestureRecognizer recognizer;
  arena.Add(&recognizer);
  SendPointerEvent(&arena, {1, Phase::ADD, {}}, 0);
  recognizer.participation_token()->Accept();

  recognizer.SetHandleEvent([&](const auto&) { recognizer.participation_token().reset(); });
  SendPointerEvent(&arena, {1, Phase::MOVE, {}}, 0);

  FX_CHECK(!recognizer.participation_token());
}

}  // namespace
}  // namespace accessibility_test
