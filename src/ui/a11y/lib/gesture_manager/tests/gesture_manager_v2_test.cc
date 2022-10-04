// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_manager_v2.h"

#include <fuchsia/ui/pointer/augment/cpp/fidl.h>
#include <fuchsia/ui/pointer/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/syslog/cpp/macros.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/a11y/lib/gesture_manager/arena/gesture_arena.h"

namespace accessibility_test {
namespace {

using a11y::GestureArena;
using a11y::GestureManagerV2;
using fidl::Binding;
using fidl::InterfaceRequest;
using fuchsia::ui::pointer::EventPhase;
using fuchsia::ui::pointer::TouchDeviceInfo;
using fuchsia::ui::pointer::TouchEvent;
using fuchsia::ui::pointer::TouchInteractionId;
using fuchsia::ui::pointer::TouchPointerSample;
using fuchsia::ui::pointer::TouchResponse;
using fuchsia::ui::pointer::TouchResponseType;
using fuchsia::ui::pointer::ViewParameters;
using fuchsia::ui::pointer::augment::TouchEventWithLocalHit;
using fuchsia::ui::pointer::augment::TouchSourceWithLocalHit;
using fuchsia::ui::pointer::augment::TouchSourceWithLocalHitPtr;

TouchEventWithLocalHit fake_touch_event(EventPhase phase, uint32_t interaction_id = 0) {
  TouchPointerSample sample;
  sample.set_interaction({0, 0, interaction_id});
  sample.set_phase(phase);
  sample.set_position_in_viewport({0, 0});

  TouchEvent inner;
  inner.set_timestamp(0);
  inner.set_pointer_sample(std::move(sample));
  inner.set_trace_flow_id(0);

  return {std::move(inner), 0, {0, 0}};
}

std::vector<TouchEventWithLocalHit> n_events(uint64_t n) {
  std::vector<TouchEventWithLocalHit> events(n);
  for (uint32_t i = 0; i < n; ++i) {
    events[i] = fake_touch_event(EventPhase::CHANGE);
  }
  return events;
}

TouchEventWithLocalHit fake_view_parameters() {
  const ViewParameters parameters = {
      /* view= */ {{0, 0}, {1, 1}},
      /* viewport= */ {{0, 0}, {1, 1}},
      /* viewport_to_view_transform= */ {0},
  };

  TouchEvent inner;
  inner.set_view_parameters(parameters);

  return {/* touch_event= */ std::move(inner),
          /* local_viewref_koid= */ 0,
          /* local_point= */ {0, 0}};
}

bool interaction_equals(TouchInteractionId id1, TouchInteractionId id2) {
  return id1.device_id == id2.device_id && id1.pointer_id == id2.pointer_id &&
         id1.interaction_id == id2.interaction_id;
}

class FakeTouchSource : public TouchSourceWithLocalHit {
 public:
  explicit FakeTouchSource(InterfaceRequest<TouchSourceWithLocalHit> server_end)
      : connected_client_(this, std::move(server_end)) {}

  ~FakeTouchSource() override = default;

  // |fuchsia::ui::pointer::augment::TouchSourceWithLocalHit|
  void Watch(std::vector<TouchResponse> responses, WatchCallback callback) override {
    ++num_watch_calls_;
    responses_ = std::move(responses);
    callback_ = std::move(callback);
  }

  // |fuchsia::ui::pointer::augment::TouchSourceWithLocalHit|
  void UpdateResponse(TouchInteractionId interaction, TouchResponse response,
                      UpdateResponseCallback callback) override {
    updated_responses_.emplace_back(std::make_pair(interaction, std::move(response)));
  }

  uint32_t NumWatchCalls() const { return num_watch_calls_; }

  void SimulateEvents(std::vector<TouchEventWithLocalHit> events) {
    FX_CHECK(callback_);
    callback_(std::move(events));
    callback_ = nullptr;
  }

  std::vector<TouchResponse> TakeResponses() { return std::move(responses_); }

  std::vector<std::pair<TouchInteractionId, TouchResponse>> TakeUpdatedResponses() {
    return std::move(updated_responses_);
  }

 private:
  uint32_t num_watch_calls_ = 0;
  std::vector<TouchResponse> responses_;
  std::vector<std::pair<TouchInteractionId, TouchResponse>> updated_responses_;
  WatchCallback callback_;

  Binding<TouchSourceWithLocalHit> connected_client_;
};

class FakeGestureArena : public GestureArena {
 public:
  // |GestureArena|
  void OnEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) override {}

  void SetFutureStates(std::deque<GestureArena::State> future_states) {
    FX_CHECK(future_states_.empty());
    future_states_ = std::move(future_states);
  }

  // |GestureArena|
  GestureArena::State GetState() override {
    FX_CHECK(!future_states_.empty());
    auto state = future_states_.front();
    future_states_.pop_front();
    return state;
  }

 private:
  std::deque<GestureArena::State> future_states_;
};

class GestureManagerV2Test : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    gtest::TestLoopFixture::SetUp();

    TouchSourceWithLocalHitPtr client_end;
    auto server_end = client_end.NewRequest();
    fake_touch_source_ = std::make_unique<FakeTouchSource>(std::move(server_end));
    auto fake_arena = std::make_unique<FakeGestureArena>();
    fake_arena_ptr_ = fake_arena.get();
    gesture_manager_ =
        std::make_unique<GestureManagerV2>(std::move(client_end), std::move(fake_arena));
  }

 protected:
  std::unique_ptr<FakeTouchSource> fake_touch_source_;
  FakeGestureArena* fake_arena_ptr_;
  std::unique_ptr<GestureManagerV2> gesture_manager_;
};

TEST_F(GestureManagerV2Test, RespondToTouchEvents) {
  // Gesture manager should call `Watch` in its constructor.
  RunLoopUntilIdle();
  EXPECT_EQ(fake_touch_source_->NumWatchCalls(), 1u);

  std::vector<TouchEventWithLocalHit> events;
  events.emplace_back(fake_view_parameters());
  fake_touch_source_->SimulateEvents(std::move(events));
  RunLoopUntilIdle();

  for (const uint32_t n : {3, 0, 1}) {
    auto events = n_events(n);
    std::deque<GestureArena::State> future_states;
    for (uint32_t i = 0; i < n; ++i) {
      future_states.emplace_back(GestureArena::State::kInProgress);
    }
    fake_arena_ptr_->SetFutureStates(future_states);
    fake_touch_source_->SimulateEvents(std::move(events));

    RunLoopUntilIdle();
    auto responses = fake_touch_source_->TakeResponses();

    EXPECT_EQ(responses.size(), n);
    for (uint32_t i = 0; i < n; ++i) {
      EXPECT_TRUE(responses[i].has_response_type());
      EXPECT_TRUE(responses[i].has_trace_flow_id());
    }
  }
}

TEST_F(GestureManagerV2Test, SimulateOneFingerSingleTap) {
  RunLoopUntilIdle();

  std::vector<TouchEventWithLocalHit> events;
  events.emplace_back(fake_view_parameters());
  fake_touch_source_->SimulateEvents(std::move(events));
  RunLoopUntilIdle();

  fake_arena_ptr_->SetFutureStates({GestureArena::State::kInProgress});
  events.clear();
  events.emplace_back(fake_touch_event(EventPhase::ADD));
  fake_touch_source_->SimulateEvents(std::move(events));
  RunLoopUntilIdle();
  std::vector<TouchResponse> responses = fake_touch_source_->TakeResponses();
  EXPECT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].response_type(), TouchResponseType::MAYBE_PRIORITIZE_SUPPRESS);

  fake_arena_ptr_->SetFutureStates({GestureArena::State::kWinnerAssigned});
  events.clear();
  events.emplace_back(fake_touch_event(EventPhase::CHANGE));
  fake_touch_source_->SimulateEvents(std::move(events));
  RunLoopUntilIdle();
  responses = fake_touch_source_->TakeResponses();
  EXPECT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].response_type(), TouchResponseType::YES_PRIORITIZE);

  fake_arena_ptr_->SetFutureStates({GestureArena::State::kWinnerAssigned});
  events.clear();
  events.emplace_back(fake_touch_event(EventPhase::REMOVE));
  fake_touch_source_->SimulateEvents(std::move(events));
  RunLoopUntilIdle();
  responses = fake_touch_source_->TakeResponses();
  EXPECT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].response_type(), TouchResponseType::YES_PRIORITIZE);

  auto updated_responses = fake_touch_source_->TakeUpdatedResponses();
  EXPECT_EQ(updated_responses.size(), 0u);
}

// This tests that we correctly use TouchSource.UpdateResponse to claim an interaction
// earlier in the gesture, after initially responding "HOLD".
TEST_F(GestureManagerV2Test, SimulateOneFingerDoubleTap) {
  RunLoopUntilIdle();

  fake_arena_ptr_->SetFutureStates({
      GestureArena::State::kInProgress,
      GestureArena::State::kInProgress,
      GestureArena::State::kInProgress,
      GestureArena::State::kContestEndedWinnerAssigned,
  });
  std::vector<TouchEventWithLocalHit> events;
  events.emplace_back(fake_view_parameters());
  events.emplace_back(fake_touch_event(EventPhase::ADD, 0));
  events.emplace_back(fake_touch_event(EventPhase::REMOVE, 0));
  events.emplace_back(fake_touch_event(EventPhase::ADD, 1));
  events.emplace_back(fake_touch_event(EventPhase::REMOVE, 1));
  auto first_interaction = events[1].touch_event.pointer_sample().interaction();

  fake_touch_source_->SimulateEvents(std::move(events));
  RunLoopUntilIdle();

  std::vector<TouchResponse> responses = fake_touch_source_->TakeResponses();
  EXPECT_EQ(responses.size(), 5u);
  EXPECT_FALSE(responses[0].has_response_type());
  EXPECT_EQ(responses[1].response_type(), TouchResponseType::MAYBE_PRIORITIZE_SUPPRESS);
  EXPECT_EQ(responses[2].response_type(), TouchResponseType::HOLD_SUPPRESS);
  EXPECT_EQ(responses[3].response_type(), TouchResponseType::MAYBE_PRIORITIZE_SUPPRESS);
  EXPECT_EQ(responses[4].response_type(), TouchResponseType::YES_PRIORITIZE);

  auto updated_responses = fake_touch_source_->TakeUpdatedResponses();
  EXPECT_EQ(updated_responses.size(), 1u);
  EXPECT_TRUE(interaction_equals(updated_responses[0].first, first_interaction));
  EXPECT_EQ(updated_responses[0].second.response_type(), TouchResponseType::YES_PRIORITIZE);
}

TEST_F(GestureManagerV2Test, SimulateTwoFingerDoubleTap) {
  RunLoopUntilIdle();

  fake_arena_ptr_->SetFutureStates({
      GestureArena::State::kInProgress,
      GestureArena::State::kInProgress,
      GestureArena::State::kInProgress,
      GestureArena::State::kInProgress,
      GestureArena::State::kInProgress,
      GestureArena::State::kInProgress,
      GestureArena::State::kInProgress,
      GestureArena::State::kContestEndedWinnerAssigned,
  });
  std::vector<TouchEventWithLocalHit> events;
  events.emplace_back(fake_view_parameters());
  events.emplace_back(fake_touch_event(EventPhase::ADD, 0));
  events.emplace_back(fake_touch_event(EventPhase::ADD, 1));
  events.emplace_back(fake_touch_event(EventPhase::REMOVE, 0));
  events.emplace_back(fake_touch_event(EventPhase::REMOVE, 1));
  events.emplace_back(fake_touch_event(EventPhase::ADD, 0));
  events.emplace_back(fake_touch_event(EventPhase::ADD, 1));
  events.emplace_back(fake_touch_event(EventPhase::REMOVE, 1));
  events.emplace_back(fake_touch_event(EventPhase::REMOVE, 0));
  auto first_interaction = events[1].touch_event.pointer_sample().interaction();
  auto second_interaction = events[2].touch_event.pointer_sample().interaction();
  auto fourth_interaction = events[6].touch_event.pointer_sample().interaction();

  fake_touch_source_->SimulateEvents(std::move(events));
  RunLoopUntilIdle();

  std::vector<TouchResponse> responses = fake_touch_source_->TakeResponses();
  EXPECT_EQ(responses.size(), 9u);
  EXPECT_FALSE(responses[0].has_response_type());
  EXPECT_EQ(responses[1].response_type(), TouchResponseType::MAYBE_PRIORITIZE_SUPPRESS);
  EXPECT_EQ(responses[2].response_type(), TouchResponseType::MAYBE_PRIORITIZE_SUPPRESS);
  EXPECT_EQ(responses[3].response_type(), TouchResponseType::HOLD_SUPPRESS);
  EXPECT_EQ(responses[4].response_type(), TouchResponseType::HOLD_SUPPRESS);
  EXPECT_EQ(responses[5].response_type(), TouchResponseType::MAYBE_PRIORITIZE_SUPPRESS);
  EXPECT_EQ(responses[6].response_type(), TouchResponseType::MAYBE_PRIORITIZE_SUPPRESS);
  EXPECT_EQ(responses[7].response_type(), TouchResponseType::HOLD_SUPPRESS);
  EXPECT_EQ(responses[8].response_type(), TouchResponseType::YES_PRIORITIZE);

  auto updated_responses = fake_touch_source_->TakeUpdatedResponses();
  EXPECT_EQ(updated_responses.size(), 3u);
  EXPECT_TRUE(interaction_equals(updated_responses[0].first, first_interaction));
  EXPECT_EQ(updated_responses[0].second.response_type(), TouchResponseType::YES_PRIORITIZE);
  EXPECT_TRUE(interaction_equals(updated_responses[1].first, second_interaction));
  EXPECT_EQ(updated_responses[1].second.response_type(), TouchResponseType::YES_PRIORITIZE);
  EXPECT_TRUE(interaction_equals(updated_responses[2].first, fourth_interaction));
  EXPECT_EQ(updated_responses[2].second.response_type(), TouchResponseType::YES_PRIORITIZE);
}

}  // namespace
}  // namespace accessibility_test
