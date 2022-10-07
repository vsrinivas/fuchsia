// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/touch_source.h"

#include <lib/async-testing/test_loop.h>
#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace input::test {

using fup_EventPhase = fuchsia::ui::pointer::EventPhase;
using fuchsia::ui::pointer::TouchResponseType;
using scenic_impl::input::ContenderId;
using scenic_impl::input::Extents;
using scenic_impl::input::GestureResponse;
using scenic_impl::input::InternalTouchEvent;
using scenic_impl::input::Phase;
using scenic_impl::input::StreamId;
using scenic_impl::input::TouchSource;
using scenic_impl::input::Viewport;

constexpr zx_koid_t kViewRefKoid = 25;
constexpr StreamId kStreamId = 1;
constexpr uint32_t kDeviceId = 2;
constexpr uint32_t kPointerId = 3;

constexpr view_tree::BoundingBox kEmptyBoundingBox{};
constexpr bool kStreamOngoing = false;
constexpr bool kStreamEnding = true;

namespace {

fuchsia::ui::pointer::TouchResponse CreateResponse(TouchResponseType response_type) {
  fuchsia::ui::pointer::TouchResponse response;
  response.set_response_type(response_type);
  return response;
}

void ExpectEqual(const fuchsia::ui::pointer::ViewParameters& received_view_parameters,
                 const Viewport& expected_viewport,
                 const view_tree::BoundingBox expected_view_bounds) {
  EXPECT_THAT(
      received_view_parameters.viewport.min,
      testing::ElementsAre(expected_viewport.extents.min[0], expected_viewport.extents.min[1]));
  EXPECT_THAT(
      received_view_parameters.viewport.max,
      testing::ElementsAre(expected_viewport.extents.max[0], expected_viewport.extents.max[1]));

  EXPECT_THAT(received_view_parameters.view.min,
              testing::ElementsAreArray(expected_view_bounds.min));
  EXPECT_THAT(received_view_parameters.view.max,
              testing::ElementsAreArray(expected_view_bounds.max));

  ASSERT_TRUE(expected_viewport.receiver_from_viewport_transform.has_value());
  EXPECT_THAT(
      received_view_parameters.viewport_to_view_transform,
      testing::ElementsAreArray(expected_viewport.receiver_from_viewport_transform.value()));
}

InternalTouchEvent IPEventTemplate(Phase phase) {
  return {
      .device_id = kDeviceId,
      .pointer_id = kPointerId,
      .phase = phase,
      .viewport =
          {
              .receiver_from_viewport_transform = std::array<float, 9>(),
          },
  };
}

}  // namespace

class TouchSourceTest : public gtest::TestLoopFixture {
 protected:
  void SetUp() override {
    client_ptr_.set_error_handler([this](auto) { channel_closed_ = true; });

    touch_source_.emplace(
        kViewRefKoid, client_ptr_.NewRequest(),
        /*respond*/
        [this](StreamId stream_id, const std::vector<GestureResponse>& responses) {
          std::copy(responses.begin(), responses.end(),
                    std::back_inserter(received_responses_[stream_id]));
        },
        /*error_handler*/ [this] { internal_error_handler_fired_ = true; }, inspector_);
  }

  bool internal_error_handler_fired_ = false;
  bool channel_closed_ = false;
  std::unordered_map<StreamId, std::vector<GestureResponse>> received_responses_;

  fuchsia::ui::pointer::TouchSourcePtr client_ptr_;
  std::optional<TouchSource> touch_source_;
  scenic_impl::input::GestureContenderInspector inspector_ =
      scenic_impl::input::GestureContenderInspector(inspect::Node());
};

TEST_F(TouchSourceTest, Watch_WithNoPendingMessages_ShouldNeverReturn) {
  bool callback_triggered = false;
  client_ptr_->Watch({}, [&callback_triggered](auto) { callback_triggered = true; });

  RunLoopUntilIdle();
  EXPECT_TRUE(received_responses_.empty());
  EXPECT_FALSE(channel_closed_);
  EXPECT_FALSE(callback_triggered);
}

TEST_F(TouchSourceTest, ErrorHandler_ShouldFire_OnClientDisconnect) {
  EXPECT_FALSE(internal_error_handler_fired_);
  client_ptr_.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(internal_error_handler_fired_);
}

TEST_F(TouchSourceTest, NonEmptyResponse_ForInitialWatch_ShouldCloseChannel) {
  bool callback_triggered = false;
  std::vector<fuchsia::ui::pointer::TouchResponse> responses;
  responses.emplace_back(CreateResponse(TouchResponseType::MAYBE));
  client_ptr_->Watch(std::move(responses),
                     [&callback_triggered](auto) { callback_triggered = true; });

  RunLoopUntilIdle();
  EXPECT_TRUE(received_responses_.empty());
  EXPECT_TRUE(channel_closed_);
  EXPECT_FALSE(callback_triggered);
}

TEST_F(TouchSourceTest, ForcedChannelClosing_ShouldFireInternalErrorHandler) {
  bool callback_triggered = false;
  std::vector<fuchsia::ui::pointer::TouchResponse> responses;
  responses.emplace_back(CreateResponse(TouchResponseType::MAYBE));
  client_ptr_->Watch(std::move(responses),
                     [&callback_triggered](auto) { callback_triggered = true; });

  EXPECT_FALSE(channel_closed_);
  EXPECT_FALSE(internal_error_handler_fired_);

  RunLoopUntilIdle();
  EXPECT_TRUE(received_responses_.empty());
  EXPECT_FALSE(callback_triggered);
  EXPECT_TRUE(channel_closed_);
  EXPECT_TRUE(internal_error_handler_fired_);
}

TEST_F(TouchSourceTest, EmptyResponse_ForPointerEvent_ShouldCloseChannel) {
  touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kAdd), kStreamOngoing,
                              kEmptyBoundingBox);
  client_ptr_->Watch({}, [](auto events) { EXPECT_EQ(events.size(), 1u); });
  RunLoopUntilIdle();

  // Respond with an empty response table.
  bool callback_triggered = false;
  std::vector<fuchsia::ui::pointer::TouchResponse> responses;
  responses.push_back({});  // Empty response.
  client_ptr_->Watch(std::move(responses),
                     [&callback_triggered](auto) { callback_triggered = true; });
  RunLoopUntilIdle();
  EXPECT_TRUE(received_responses_.empty());
  EXPECT_FALSE(callback_triggered);
  EXPECT_TRUE(channel_closed_);
}

TEST_F(TouchSourceTest, NonEmptyResponse_ForNonPointerEvent_ShouldCloseChannel) {
  touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kAdd), kStreamOngoing,
                              kEmptyBoundingBox);
  // This event expects an empty response table.
  touch_source_->EndContest(kStreamId, /*awarded_win*/ true);
  client_ptr_->Watch({}, [](auto events) { EXPECT_EQ(events.size(), 2u); });
  RunLoopUntilIdle();

  bool callback_triggered = false;
  std::vector<fuchsia::ui::pointer::TouchResponse> responses;
  responses.emplace_back(CreateResponse(TouchResponseType::MAYBE));
  responses.emplace_back(CreateResponse(TouchResponseType::MAYBE));  // Expected to be empty.
  client_ptr_->Watch(std::move(responses),
                     [&callback_triggered](auto) { callback_triggered = true; });

  RunLoopUntilIdle();
  EXPECT_TRUE(received_responses_.empty());
  EXPECT_FALSE(callback_triggered);
  EXPECT_TRUE(channel_closed_);
}

TEST_F(TouchSourceTest, Watch_BeforeEvents_ShouldReturnOnFirstEvent) {
  uint64_t num_events = 0;
  client_ptr_->Watch({}, [&num_events](auto events) { num_events += events.size(); });

  RunLoopUntilIdle();
  EXPECT_TRUE(received_responses_.empty());
  EXPECT_FALSE(channel_closed_);
  EXPECT_EQ(num_events, 0u);

  // Sending fidl message on first event, so expect the second one not to arrive.
  touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kAdd), kStreamOngoing,
                              kEmptyBoundingBox);
  touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kChange), kStreamOngoing,
                              kEmptyBoundingBox);

  RunLoopUntilIdle();
  EXPECT_TRUE(received_responses_.empty());
  EXPECT_FALSE(channel_closed_);
  EXPECT_EQ(num_events, 1u);

  // Second event should arrive on next Watch() call.
  std::vector<fuchsia::ui::pointer::TouchResponse> responses;
  responses.emplace_back(CreateResponse(TouchResponseType::MAYBE));
  client_ptr_->Watch(std::move(responses),
                     [&num_events](auto events) { num_events += events.size(); });
  RunLoopUntilIdle();
  EXPECT_EQ(received_responses_.size(), 1u);
  EXPECT_FALSE(channel_closed_);
  EXPECT_EQ(num_events, 2u);
}

TEST_F(TouchSourceTest, Watch_ShouldAtMostReturn_TOUCH_MAX_EVENT_Events_PerCall) {
  // Sending fidl message on first event, so expect the second one not to arrive.
  touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kAdd), kStreamOngoing,
                              kEmptyBoundingBox);
  for (size_t i = 0; i < fuchsia::ui::pointer::TOUCH_MAX_EVENT + 3; ++i) {
    touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kChange), kStreamOngoing,
                                kEmptyBoundingBox);
  }

  client_ptr_->Watch(
      {}, [](auto events) { ASSERT_EQ(events.size(), fuchsia::ui::pointer::TOUCH_MAX_EVENT); });
  RunLoopUntilIdle();

  std::vector<fuchsia::ui::pointer::TouchResponse> responses;
  for (size_t i = 0; i < fuchsia::ui::pointer::TOUCH_MAX_EVENT; ++i) {
    responses.emplace_back(CreateResponse(TouchResponseType::MAYBE));
  }

  // The 4 events remaining in the queue should be delivered with the next Watch() call.
  client_ptr_->Watch(std::move(responses), [](auto events) { EXPECT_EQ(events.size(), 4u); });
  RunLoopUntilIdle();
}

TEST_F(TouchSourceTest, Watch_ResponseBeforeEvent_ShouldCloseChannel) {
  // Initial call to Watch() should be empty since we can't respond to any events yet.
  bool callback_triggered = false;
  std::vector<fuchsia::ui::pointer::TouchResponse> responses;
  responses.emplace_back(CreateResponse(TouchResponseType::MAYBE));
  client_ptr_->Watch(std::move(responses),
                     [&callback_triggered](auto) { callback_triggered = true; });

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_triggered);
  EXPECT_TRUE(channel_closed_);
}

TEST_F(TouchSourceTest, Watch_MoreResponsesThanEvents_ShouldCloseChannel) {
  touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kAdd), kStreamOngoing,
                              kEmptyBoundingBox);
  client_ptr_->Watch({}, [](auto events) { EXPECT_EQ(events.size(), 1u); });
  RunLoopUntilIdle();
  EXPECT_FALSE(channel_closed_);

  // Expecting one response. Send two.
  bool callback_fired = false;
  std::vector<fuchsia::ui::pointer::TouchResponse> responses;
  responses.emplace_back(CreateResponse(TouchResponseType::MAYBE));
  responses.emplace_back(CreateResponse(TouchResponseType::MAYBE));
  client_ptr_->Watch(std::move(responses), [&callback_fired](auto) { callback_fired = true; });

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_fired);
  EXPECT_TRUE(channel_closed_);
}

TEST_F(TouchSourceTest, Watch_FewerResponsesThanEvents_ShouldCloseChannel) {
  touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kAdd), kStreamOngoing,
                              kEmptyBoundingBox);
  touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kChange), kStreamOngoing,
                              kEmptyBoundingBox);
  client_ptr_->Watch({}, [](auto events) { EXPECT_EQ(events.size(), 2u); });
  RunLoopUntilIdle();
  EXPECT_FALSE(channel_closed_);

  // Expecting two responses. Send one.
  bool callback_fired = false;
  std::vector<fuchsia::ui::pointer::TouchResponse> responses;
  responses.emplace_back(CreateResponse(TouchResponseType::MAYBE));
  client_ptr_->Watch(std::move(responses), [&callback_fired](auto) { callback_fired = true; });

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_fired);
  EXPECT_TRUE(channel_closed_);
}

TEST_F(TouchSourceTest, Watch_CallingTwiceWithoutWaiting_ShouldCloseChannel) {
  client_ptr_->Watch({}, [](auto) { EXPECT_FALSE(true); });
  client_ptr_->Watch({}, [](auto) { EXPECT_FALSE(true); });
  RunLoopUntilIdle();
  EXPECT_TRUE(channel_closed_);
}

TEST_F(TouchSourceTest, MissingArgument_ShouldCloseChannel) {
  uint64_t num_events = 0;
  client_ptr_->Watch({}, [&num_events](auto events) { num_events += events.size(); });
  RunLoopUntilIdle();
  EXPECT_EQ(num_events, 0u);
  EXPECT_FALSE(channel_closed_);

  touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kAdd), kStreamOngoing,
                              kEmptyBoundingBox);
  RunLoopUntilIdle();
  EXPECT_EQ(num_events, 1u);
  EXPECT_FALSE(channel_closed_);

  std::vector<fuchsia::ui::pointer::TouchResponse> responses;
  responses.emplace_back();  // Empty response for pointer event should close channel.
  client_ptr_->Watch(std::move(responses),
                     [&num_events](auto events) { num_events += events.size(); });

  RunLoopUntilIdle();
  EXPECT_EQ(num_events, 1u);
  EXPECT_TRUE(channel_closed_);
}

TEST_F(TouchSourceTest, UpdateResponse) {
  {  // Complete a stream and respond HOLD to it.
    client_ptr_->Watch({}, [](auto) {});
    touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kAdd), kStreamOngoing,
                                kEmptyBoundingBox);
    touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kRemove), kStreamEnding,
                                kEmptyBoundingBox);
    RunLoopUntilIdle();

    std::vector<fuchsia::ui::pointer::TouchResponse> responses;
    responses.emplace_back(CreateResponse(TouchResponseType::HOLD));
    responses.emplace_back(CreateResponse(TouchResponseType::HOLD));
    client_ptr_->Watch(std::move(responses), [](auto) {});
    RunLoopUntilIdle();
  }

  {
    bool callback_triggered = false;
    client_ptr_->UpdateResponse(
        fuchsia::ui::pointer::TouchInteractionId{
            .device_id = kDeviceId,
            .pointer_id = kPointerId,
            .interaction_id = kStreamId,
        },
        CreateResponse(TouchResponseType::YES),
        [&callback_triggered] { callback_triggered = true; });
    RunLoopUntilIdle();
    EXPECT_TRUE(callback_triggered);
    EXPECT_FALSE(channel_closed_);
  }
}

TEST_F(TouchSourceTest, UpdateResponse_UnknownStreamId_ShouldCloseChannel) {
  bool callback_triggered = false;
  client_ptr_->UpdateResponse(
      fuchsia::ui::pointer::TouchInteractionId{
          .device_id = 1,
          .pointer_id = 1,
          .interaction_id = 12153,  // Unknown stream id.
      },
      CreateResponse(TouchResponseType::YES), [&callback_triggered] { callback_triggered = true; });

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_triggered);
  EXPECT_TRUE(channel_closed_);
  EXPECT_TRUE(received_responses_.empty());
}

TEST_F(TouchSourceTest, UpdateResponse_BeforeStreamEnd_ShouldCloseChannel) {
  {  // Start a stream and respond to it.
    bool callback_triggered = false;
    client_ptr_->Watch({}, [&callback_triggered](auto) { callback_triggered = true; });
    touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kAdd), kStreamOngoing,
                                kEmptyBoundingBox);
    RunLoopUntilIdle();
    EXPECT_TRUE(callback_triggered);

    std::vector<fuchsia::ui::pointer::TouchResponse> responses;
    responses.emplace_back(CreateResponse(TouchResponseType::HOLD));
    client_ptr_->Watch(std::move(responses), [](auto) {});
    RunLoopUntilIdle();
  }

  {  // Try to reject the stream despite it not having ended.
    bool callback_triggered = false;
    client_ptr_->UpdateResponse(
        fuchsia::ui::pointer::TouchInteractionId{
            .device_id = 1,
            .pointer_id = 1,
            .interaction_id = kStreamId,
        },
        CreateResponse(TouchResponseType::YES),
        [&callback_triggered] { callback_triggered = true; });
    RunLoopUntilIdle();
    EXPECT_FALSE(callback_triggered);
    EXPECT_TRUE(channel_closed_);
  }
}

TEST_F(TouchSourceTest, UpdateResponse_WhenLastResponseWasntHOLD_ShouldCloseChannel) {
  {  // Start a stream and respond to it.
    bool callback_triggered = false;
    client_ptr_->Watch({}, [&callback_triggered](auto) { callback_triggered = true; });
    touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kAdd), kStreamOngoing,
                                kEmptyBoundingBox);
    touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kRemove), kStreamEnding,
                                kEmptyBoundingBox);
    RunLoopUntilIdle();
    EXPECT_TRUE(callback_triggered);

    bool callback2_triggered = false;
    std::vector<fuchsia::ui::pointer::TouchResponse> responses;
    // Respond with something other than HOLD.
    responses.emplace_back(CreateResponse(TouchResponseType::MAYBE));
    responses.emplace_back(CreateResponse(TouchResponseType::MAYBE));
    client_ptr_->Watch(std::move(responses), [](auto) {});
    RunLoopUntilIdle();
  }

  {
    bool callback_triggered = false;
    client_ptr_->UpdateResponse(
        fuchsia::ui::pointer::TouchInteractionId{
            .device_id = 1,
            .pointer_id = 1,
            .interaction_id = kStreamId,
        },
        CreateResponse(TouchResponseType::YES),
        [&callback_triggered] { callback_triggered = true; });
    RunLoopUntilIdle();
    EXPECT_FALSE(callback_triggered);
    EXPECT_TRUE(channel_closed_);
  }
}

TEST_F(TouchSourceTest, UpdateResponse_WithHOLD_ShouldCloseChannel) {
  {  // Start a stream and respond to it.
    bool callback_triggered = false;
    client_ptr_->Watch({}, [&callback_triggered](auto) { callback_triggered = true; });
    touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kAdd), kStreamOngoing,
                                kEmptyBoundingBox);
    touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kRemove), kStreamEnding,
                                kEmptyBoundingBox);
    RunLoopUntilIdle();
    EXPECT_TRUE(callback_triggered);

    std::vector<fuchsia::ui::pointer::TouchResponse> responses;
    responses.emplace_back(CreateResponse(TouchResponseType::HOLD));
    responses.emplace_back(CreateResponse(TouchResponseType::HOLD));
    client_ptr_->Watch(std::move(responses), [](auto) {});
    RunLoopUntilIdle();
  }

  {  // Try to update the stream with a HOLD response.
    bool callback_triggered = false;
    client_ptr_->UpdateResponse(
        fuchsia::ui::pointer::TouchInteractionId{
            .device_id = 1,
            .pointer_id = 1,
            .interaction_id = kStreamId,
        },
        CreateResponse(TouchResponseType::HOLD),
        [&callback_triggered] { callback_triggered = true; });
    RunLoopUntilIdle();
    EXPECT_FALSE(callback_triggered);
    EXPECT_TRUE(channel_closed_);
  }
}

TEST_F(TouchSourceTest, ViewportIsDeliveredCorrectly) {
  Viewport viewport;
  viewport.extents = std::array<std::array<float, 2>, 2>{{{0, 0}, {10, 10}}};
  viewport.receiver_from_viewport_transform = {
      // clang-format off
    1, 0, 0, // column one
    0, 1, 0, // column two
    0, 0, 1, // column three
      // clang-format on
  };
  const view_tree::BoundingBox view_bounds{.min = {5, 5}, .max = {10, 10}};

  // Submit the same viewport for all events.
  {
    auto event = IPEventTemplate(Phase::kAdd);
    event.viewport = viewport;
    touch_source_->UpdateStream(kStreamId, event, kStreamOngoing, view_bounds);
  }
  {
    auto event = IPEventTemplate(Phase::kRemove);
    event.viewport = viewport;
    touch_source_->UpdateStream(kStreamId, event, kStreamEnding, view_bounds);
  }

  client_ptr_->Watch({}, [&](auto events) {
    // Viewport should always be delivered for the first event.
    ASSERT_EQ(events.size(), 2u);
    EXPECT_TRUE(events[0].has_pointer_sample());
    ASSERT_TRUE(events[0].has_view_parameters());
    ExpectEqual(events[0].view_parameters(), viewport, view_bounds);

    // Viewport should not be delivered when no changes have been made.
    EXPECT_FALSE(events[1].has_view_parameters());
    EXPECT_TRUE(events[1].has_pointer_sample());
  });

  RunLoopUntilIdle();
}

TEST_F(TouchSourceTest, WhenExtentsChange_ViewportShouldUpdate) {
  Viewport viewport1;
  viewport1.extents = std::array<std::array<float, 2>, 2>{{{0, 0}, {10, 10}}};
  viewport1.receiver_from_viewport_transform = {
      // clang-format off
    1, 0, 0, // column one
    0, 1, 0, // column two
    0, 0, 1, // column three
      // clang-format on
  };
  const view_tree::BoundingBox view_bounds1{.min = {5, 5}, .max = {10, 10}};

  // Change only the extents.
  Viewport viewport2 = viewport1;
  viewport2.extents = std::array<std::array<float, 2>, 2>{{{-5, 1}, {100, 40}}};

  {
    auto event = IPEventTemplate(Phase::kAdd);
    event.viewport = viewport1;
    touch_source_->UpdateStream(kStreamId, event, kStreamOngoing, view_bounds1);
  }
  {  // viewport2 -> new viewport.
    auto event = IPEventTemplate(Phase::kRemove);
    event.viewport = viewport2;
    touch_source_->UpdateStream(kStreamId, event, kStreamEnding, view_bounds1);
  }

  client_ptr_->Watch({}, [&](auto events) {
    ASSERT_EQ(events.size(), 2u);
    EXPECT_TRUE(events[0].has_pointer_sample());
    ASSERT_TRUE(events[0].has_view_parameters());
    ExpectEqual(events[0].view_parameters(), viewport1, view_bounds1);

    EXPECT_TRUE(events[1].has_pointer_sample());
    ASSERT_TRUE(events[1].has_view_parameters());
    ExpectEqual(events[1].view_parameters(), viewport2, view_bounds1);
  });

  RunLoopUntilIdle();
}

TEST_F(TouchSourceTest, WhenTransformChanges_ViewportShouldUpdate) {
  Viewport viewport1;
  viewport1.extents = std::array<std::array<float, 2>, 2>{{{0, 0}, {10, 10}}};
  viewport1.receiver_from_viewport_transform = {
      // clang-format off
    1, 0, 0, // column one
    0, 1, 0, // column two
    0, 0, 1, // column three
      // clang-format on
  };
  const view_tree::BoundingBox view_bounds1{.min = {5, 5}, .max = {10, 10}};

  // Change only the transform.
  Viewport viewport2 = viewport1;
  viewport2.receiver_from_viewport_transform = {
      // clang-format off
     1,  2,  3, // column one
     4,  5,  6, // column two
     7,  8,  9  // column three
      // clang-format on
  };

  {
    auto event = IPEventTemplate(Phase::kAdd);
    event.viewport = viewport1;
    touch_source_->UpdateStream(kStreamId, event, kStreamOngoing, view_bounds1);
  }
  {  // viewport2 -> new viewport.
    auto event = IPEventTemplate(Phase::kRemove);
    event.viewport = viewport2;
    touch_source_->UpdateStream(kStreamId, event, kStreamEnding, view_bounds1);
  }

  client_ptr_->Watch({}, [&](auto events) {
    ASSERT_EQ(events.size(), 2u);
    EXPECT_TRUE(events[0].has_pointer_sample());
    ASSERT_TRUE(events[0].has_view_parameters());
    ExpectEqual(events[0].view_parameters(), viewport1, view_bounds1);

    EXPECT_TRUE(events[1].has_pointer_sample());
    ASSERT_TRUE(events[1].has_view_parameters());
    ExpectEqual(events[1].view_parameters(), viewport2, view_bounds1);
  });

  RunLoopUntilIdle();
}

TEST_F(TouchSourceTest, WhenViewBoundsChange_ViewportShouldUpdate) {
  Viewport viewport1;
  viewport1.extents = std::array<std::array<float, 2>, 2>{{{0, 0}, {10, 10}}};
  viewport1.receiver_from_viewport_transform = {
      // clang-format off
    1, 0, 0, // column one
    0, 1, 0, // column two
    0, 0, 1, // column three
      // clang-format on
  };
  const view_tree::BoundingBox view_bounds1{.min = {5, 5}, .max = {10, 10}};
  const view_tree::BoundingBox view_bounds2{.min = {-1, -2}, .max = {3, 4}};

  {
    auto event = IPEventTemplate(Phase::kAdd);
    event.viewport = viewport1;
    touch_source_->UpdateStream(kStreamId, event, kStreamOngoing, view_bounds1);
  }
  {  // view_bounds2 -> new viewport.
    auto event = IPEventTemplate(Phase::kRemove);
    event.viewport = viewport1;
    touch_source_->UpdateStream(kStreamId, event, kStreamEnding, view_bounds2);
  }

  client_ptr_->Watch({}, [&](auto events) {
    ASSERT_EQ(events.size(), 2u);
    EXPECT_TRUE(events[0].has_pointer_sample());
    ASSERT_TRUE(events[0].has_view_parameters());
    ExpectEqual(events[0].view_parameters(), viewport1, view_bounds1);

    EXPECT_TRUE(events[1].has_pointer_sample());
    ASSERT_TRUE(events[1].has_view_parameters());
    ExpectEqual(events[1].view_parameters(), viewport1, view_bounds2);
  });

  RunLoopUntilIdle();
}

// Sends a full stream and observes that GestureResponses are as expected.
TEST_F(TouchSourceTest, NormalStream) {
  touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kAdd), kStreamOngoing,
                              kEmptyBoundingBox);
  touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kChange), kStreamOngoing,
                              kEmptyBoundingBox);
  touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kChange), kStreamOngoing,
                              kEmptyBoundingBox);
  touch_source_->UpdateStream(kStreamId, IPEventTemplate(Phase::kRemove), kStreamEnding,
                              kEmptyBoundingBox);

  EXPECT_TRUE(received_responses_.empty());

  client_ptr_->Watch({}, [&](auto events) {
    ASSERT_EQ(events.size(), 4u);
    EXPECT_EQ(events[0].pointer_sample().phase(), fup_EventPhase::ADD);
    EXPECT_EQ(events[1].pointer_sample().phase(), fup_EventPhase::CHANGE);
    EXPECT_EQ(events[2].pointer_sample().phase(), fup_EventPhase::CHANGE);
    EXPECT_EQ(events[3].pointer_sample().phase(), fup_EventPhase::REMOVE);

    EXPECT_TRUE(events[0].has_timestamp());
    EXPECT_TRUE(events[1].has_timestamp());
    EXPECT_TRUE(events[2].has_timestamp());
    EXPECT_TRUE(events[3].has_timestamp());

    std::vector<fuchsia::ui::pointer::TouchResponse> responses;
    responses.emplace_back(CreateResponse(TouchResponseType::MAYBE));
    responses.emplace_back(CreateResponse(TouchResponseType::HOLD));
    responses.emplace_back(CreateResponse(TouchResponseType::HOLD));
    responses.emplace_back(CreateResponse(TouchResponseType::YES));

    client_ptr_->Watch({std::move(responses)}, [](auto events) {
      // These will be checked after EndContest() below, when the callback runs.
      EXPECT_EQ(events.size(), 1u);
      EXPECT_FALSE(events.at(0).has_pointer_sample());
      EXPECT_TRUE(events.at(0).has_timestamp());
      ASSERT_TRUE(events.at(0).has_interaction_result());

      const auto& interaction_result = events.at(0).interaction_result();
      EXPECT_EQ(interaction_result.interaction.interaction_id, kStreamId);
      EXPECT_EQ(interaction_result.interaction.device_id, kDeviceId);
      EXPECT_EQ(interaction_result.interaction.pointer_id, kPointerId);
      EXPECT_EQ(interaction_result.status, fuchsia::ui::pointer::TouchInteractionStatus::GRANTED);
    });
  });

  RunLoopUntilIdle();
  EXPECT_EQ(received_responses_.size(), 1u);
  EXPECT_THAT(received_responses_[kStreamId],
              testing::ElementsAre(GestureResponse::kMaybe, GestureResponse::kHold,
                                   GestureResponse::kHold, GestureResponse::kYes));

  // Check winning conditions.
  touch_source_->EndContest(kStreamId, /*awarded_win*/ true);
  RunLoopUntilIdle();
}

TEST_F(TouchSourceTest, TouchDeviceInfo_ShouldBeSent_OncePerDevice) {
  const uint32_t kDeviceId1 = 11111, kDeviceId2 = 22222;

  // Start three separate streams, two with the kDeviceId1 and one with kDeviceId2.
  {
    InternalTouchEvent event = IPEventTemplate(Phase::kAdd);
    event.device_id = kDeviceId1;
    touch_source_->UpdateStream(/*stream_id*/ 1, event, kStreamOngoing, kEmptyBoundingBox);
  }
  {
    InternalTouchEvent event = IPEventTemplate(Phase::kAdd);
    event.device_id = kDeviceId1;
    touch_source_->UpdateStream(/*stream_id*/ 2, event, kStreamOngoing, kEmptyBoundingBox);
  }
  {
    InternalTouchEvent event = IPEventTemplate(Phase::kAdd);
    event.device_id = kDeviceId2;
    touch_source_->UpdateStream(/*stream_id*/ 3, event, kStreamOngoing, kEmptyBoundingBox);
  }
  RunLoopUntilIdle();

  {  // Only the first instance of each device_id should generate a device_info parameter.
    std::vector<fuchsia::ui::pointer::TouchEvent> received_events;
    client_ptr_->Watch({},
                       [&received_events](auto events) { received_events = std::move(events); });
    RunLoopUntilIdle();

    ASSERT_EQ(received_events.size(), 3u);

    ASSERT_TRUE(received_events[0].has_device_info());
    EXPECT_EQ(received_events[0].device_info().id(), kDeviceId1);
    ASSERT_TRUE(received_events[0].has_pointer_sample());
    EXPECT_EQ(received_events[0].pointer_sample().interaction().device_id, kDeviceId1);

    ASSERT_FALSE(received_events[1].has_device_info());
    ASSERT_TRUE(received_events[1].has_pointer_sample());
    EXPECT_EQ(received_events[1].pointer_sample().interaction().device_id, kDeviceId1);

    ASSERT_TRUE(received_events[2].has_device_info());
    EXPECT_EQ(received_events[2].device_info().id(), kDeviceId2);
    ASSERT_TRUE(received_events[2].has_pointer_sample());
    EXPECT_EQ(received_events[2].pointer_sample().interaction().device_id, kDeviceId2);
  }
}

}  // namespace input::test
