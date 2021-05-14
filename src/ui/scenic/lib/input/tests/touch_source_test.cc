// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/touch_source.h"

#include <lib/async-testing/test_loop.h>
#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lib/gtest/test_loop_fixture.h"

namespace lib_ui_input_tests {
namespace {

using fup_EventPhase = fuchsia::ui::pointer::EventPhase;
using fuchsia::ui::pointer::TouchResponseType;
using scenic_impl::input::ContenderId;
using scenic_impl::input::Extents;
using scenic_impl::input::GestureResponse;
using scenic_impl::input::InternalPointerEvent;
using scenic_impl::input::Phase;
using scenic_impl::input::StreamId;
using scenic_impl::input::TouchSource;
using scenic_impl::input::Viewport;

constexpr StreamId kStreamId = 1;
constexpr uint32_t kDeviceId = 2;
constexpr uint32_t kPointerId = 3;

namespace {

fuchsia::ui::pointer::TouchResponse CreateResponse(TouchResponseType response_type) {
  fuchsia::ui::pointer::TouchResponse response;
  response.set_response_type(response_type);
  return response;
}

void ExpectEqual(const fuchsia::ui::pointer::ViewParameters& view_parameters,
                 const Viewport& viewport) {
  EXPECT_THAT(view_parameters.viewport.min,
              testing::ElementsAre(viewport.extents.min[0], viewport.extents.min[1]));
  EXPECT_THAT(view_parameters.viewport.max,
              testing::ElementsAre(viewport.extents.max[0], viewport.extents.max[1]));

  const auto& mat = viewport.context_from_viewport_transform;
  EXPECT_THAT(view_parameters.viewport_to_view_transform,
              testing::ElementsAre(mat[0][0], mat[0][1], mat[0][2], mat[1][0], mat[1][1], mat[1][2],
                                   mat[2][0], mat[2][1], mat[2][2]));
}

}  // namespace

class TouchSourceTest : public gtest::TestLoopFixture {
 protected:
  void SetUp() override {
    client_ptr_.set_error_handler([this](auto) { channel_closed_ = true; });

    touch_source_.emplace(
        client_ptr_.NewRequest(),
        /*respond*/
        [this](StreamId stream_id, const std::vector<GestureResponse>& responses) {
          std::copy(responses.begin(), responses.end(),
                    std::back_inserter(received_responses_[stream_id]));
        },
        /*error_handler*/ [this] { internal_error_handler_fired_ = true; });
  }

  bool internal_error_handler_fired_ = false;
  bool channel_closed_ = false;
  std::unordered_map<StreamId, std::vector<GestureResponse>> received_responses_;

  fuchsia::ui::pointer::TouchSourcePtr client_ptr_;
  std::optional<TouchSource> touch_source_;
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
  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kAdd}, /*is_end_of_stream*/ false);
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
  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kAdd}, /*is_end_of_stream*/ false);
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
  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kAdd}, /*is_end_of_stream*/ false);
  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kChange},
                              /*is_end_of_stream*/ false);

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
  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kAdd}, /*is_end_of_stream*/ false);
  for (size_t i = 0; i < fuchsia::ui::pointer::TOUCH_MAX_EVENT + 3; ++i) {
    touch_source_->UpdateStream(kStreamId, {.phase = Phase::kChange},
                                /*is_end_of_stream*/ false);
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
  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kAdd}, /*is_end_of_stream*/ false);
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
  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kAdd}, /*is_end_of_stream*/ false);
  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kChange},
                              /*is_end_of_stream*/ false);
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

  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kAdd}, /*is_end_of_stream*/ false);
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
    touch_source_->UpdateStream(kStreamId, {.phase = Phase::kAdd},
                                /*is_end_of_stream*/ false);
    touch_source_->UpdateStream(kStreamId, {.phase = Phase::kRemove},
                                /*is_end_of_stream*/ true);
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
    touch_source_->UpdateStream(kStreamId, {.phase = Phase::kAdd},
                                /*is_end_of_stream*/ false);
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
    touch_source_->UpdateStream(kStreamId, {.phase = Phase::kAdd},
                                /*is_end_of_stream*/ false);
    touch_source_->UpdateStream(kStreamId, {.phase = Phase::kRemove},
                                /*is_end_of_stream*/ true);
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
    touch_source_->UpdateStream(kStreamId, {.phase = Phase::kAdd},
                                /*is_end_of_stream*/ false);
    touch_source_->UpdateStream(kStreamId, {.phase = Phase::kRemove},
                                /*is_end_of_stream*/ true);
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
  Viewport viewport1;
  viewport1.extents = std::array<std::array<float, 2>, 2>{{{0, 0}, {10, 10}}};
  viewport1.context_from_viewport_transform = {
      // clang-format off
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1
      // clang-format on
  };
  Viewport viewport2;
  viewport2.extents = std::array<std::array<float, 2>, 2>{{{-5, 1}, {100, 40}}};
  viewport2.context_from_viewport_transform = {
      // clang-format off
    1, 2, 3, 0,
    4, 5, 6, 0,
    7, 8, 9, 0,
    0, 0, 0, 1
      // clang-format on
  };

  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kAdd, .viewport = viewport1},
                              /*is_end_of_stream*/ false);
  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kChange, .viewport = viewport1},
                              /*is_end_of_stream*/ false);
  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kRemove, .viewport = viewport2},
                              /*is_end_of_stream*/ true);

  client_ptr_->Watch({}, [&](auto events) {
    ASSERT_EQ(events.size(), 3u);
    EXPECT_TRUE(events[0].has_view_parameters());
    EXPECT_TRUE(events[0].has_pointer_sample());

    EXPECT_FALSE(events[1].has_view_parameters());
    EXPECT_TRUE(events[1].has_pointer_sample());

    EXPECT_TRUE(events[2].has_view_parameters());
    EXPECT_TRUE(events[2].has_pointer_sample());

    ExpectEqual(events[0].view_parameters(), viewport1);
    // ExpectEqual(events[2].view_parameters(), viewport2);
  });

  RunLoopUntilIdle();
}

// Sends a full stream and observes that GestureResponses are as expected.
TEST_F(TouchSourceTest, NormalStream) {
  touch_source_->UpdateStream(
      kStreamId, {.device_id = kDeviceId, .pointer_id = kPointerId, .phase = Phase::kAdd},
      /*is_end_of_stream*/ false);
  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kChange},
                              /*is_end_of_stream*/ false);
  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kChange},
                              /*is_end_of_stream*/ false);
  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kRemove},
                              /*is_end_of_stream*/ true);

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

// Sends a full legacy interaction (including UP and DOWN events) and observes that GestureResponses
// are included for the extra events not seen by clients. Each filtered event should duplicate the
// response of the previous event.
TEST_F(TouchSourceTest, LegacyInteraction) {
  touch_source_->UpdateStream(
      kStreamId, {.device_id = kDeviceId, .pointer_id = kPointerId, .phase = Phase::kAdd},
      /*is_end_of_stream*/ false);
  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kDown}, /*is_end_of_stream*/ false);
  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kChange},
                              /*is_end_of_stream*/ false);
  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kChange},
                              /*is_end_of_stream*/ false);
  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kUp}, /*is_end_of_stream*/ false);
  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kRemove},
                              /*is_end_of_stream*/ true);

  EXPECT_TRUE(received_responses_.empty());

  client_ptr_->Watch({}, [&](auto events) {
    ASSERT_EQ(events.size(), 4u);
    EXPECT_EQ(events[0].pointer_sample().phase(), fup_EventPhase::ADD);
    EXPECT_EQ(events[1].pointer_sample().phase(), fup_EventPhase::CHANGE);
    EXPECT_EQ(events[2].pointer_sample().phase(), fup_EventPhase::CHANGE);
    EXPECT_EQ(events[3].pointer_sample().phase(), fup_EventPhase::REMOVE);

    std::vector<fuchsia::ui::pointer::TouchResponse> responses;
    responses.emplace_back(CreateResponse(TouchResponseType::MAYBE));
    responses.emplace_back(CreateResponse(TouchResponseType::HOLD));
    responses.emplace_back(CreateResponse(TouchResponseType::HOLD));
    responses.emplace_back(CreateResponse(TouchResponseType::YES));
    client_ptr_->Watch({std::move(responses)}, [](auto events) {
      // These will be checked after EndContest() below.
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
  EXPECT_THAT(
      received_responses_.at(kStreamId),
      testing::ElementsAre(GestureResponse::kMaybe, GestureResponse::kMaybe, GestureResponse::kHold,
                           GestureResponse::kHold, GestureResponse::kHold, GestureResponse::kYes));

  // Check losing conditions.
  touch_source_->EndContest(kStreamId, /*awarded_win*/ true);
  RunLoopUntilIdle();
}

TEST_F(TouchSourceTest, OnDestruction_ShouldExitOngoingContests) {
  constexpr StreamId kStreamId2 = 2, kStreamId3 = 3, kStreamId4 = 4, kStreamId5 = 5, kStreamId6 = 6;

  // Start a few streams.
  touch_source_->UpdateStream(
      kStreamId, {.device_id = kDeviceId, .pointer_id = kPointerId, .phase = Phase::kAdd},
      /*is_end_of_stream*/ false);
  touch_source_->UpdateStream(
      kStreamId2, {.device_id = kDeviceId, .pointer_id = kPointerId, .phase = Phase::kAdd},
      /*is_end_of_stream*/ false);
  touch_source_->UpdateStream(
      kStreamId3, {.device_id = kDeviceId, .pointer_id = kPointerId, .phase = Phase::kAdd},
      /*is_end_of_stream*/ false);
  touch_source_->UpdateStream(
      kStreamId4, {.device_id = kDeviceId, .pointer_id = kPointerId, .phase = Phase::kAdd},
      /*is_end_of_stream*/ false);
  touch_source_->UpdateStream(
      kStreamId5, {.device_id = kDeviceId, .pointer_id = kPointerId, .phase = Phase::kAdd},
      /*is_end_of_stream*/ false);
  touch_source_->UpdateStream(
      kStreamId6, {.device_id = kDeviceId, .pointer_id = kPointerId, .phase = Phase::kAdd},
      /*is_end_of_stream*/ false);

  // End streams 1-3.
  touch_source_->UpdateStream(kStreamId, {.phase = Phase::kRemove},
                              /*is_end_of_stream*/ true);
  touch_source_->UpdateStream(kStreamId2, {.phase = Phase::kRemove},
                              /*is_end_of_stream*/ true);
  touch_source_->UpdateStream(kStreamId3, {.phase = Phase::kRemove},
                              /*is_end_of_stream*/ true);

  // Award some wins and losses.
  touch_source_->EndContest(kStreamId, /*awarded_win*/ true);
  touch_source_->EndContest(kStreamId2, /*awarded_win*/ false);
  touch_source_->EndContest(kStreamId4, /*awarded_win*/ true);
  touch_source_->EndContest(kStreamId5, /*awarded_win*/ false);

  // We now have streams in the following states:
  // 1: Ended, Won
  // 2: Ended, Lost
  // 3: Ended, Undecided
  // 4: Ongoing, Won
  // 5: Ongoing, Lost
  // 6: Ongoing, Undecided
  //
  // TouchSource should respond only to undecided streams on destruction.

  EXPECT_TRUE(received_responses_.empty());

  // Destroy the event source and observe proper cleanup.
  touch_source_.reset();

  EXPECT_EQ(received_responses_.size(), 2u);
  EXPECT_THAT(received_responses_.at(kStreamId3), testing::ElementsAre(GestureResponse::kNo));
  EXPECT_THAT(received_responses_.at(kStreamId6), testing::ElementsAre(GestureResponse::kNo));
}

// Checks that a response to an already ended stream doesn't respond to the gesture arena.
TEST_F(TouchSourceTest, WatchAfterContestEnd_ShouldNotRespond) {
  constexpr StreamId kStreamId2 = 2, kStreamId3 = 3, kStreamId4 = 4, kStreamId5 = 5, kStreamId6 = 6;

  client_ptr_->Watch({}, [](auto) {});

  // Start a stream, then end the contest before receiving responses.
  touch_source_->UpdateStream(
      kStreamId, {.device_id = kDeviceId, .pointer_id = kPointerId, .phase = Phase::kAdd},
      /*is_end_of_stream*/ false);
  RunLoopUntilIdle();
  touch_source_->EndContest(kStreamId, /*awarded_win*/ false);
  RunLoopUntilIdle();

  // Now respond to the already ended stream.
  std::vector<fuchsia::ui::pointer::TouchResponse> responses;
  responses.emplace_back(CreateResponse(TouchResponseType::MAYBE));
  bool callback_triggered = false;
  client_ptr_->Watch(std::move(responses),
                     [&callback_triggered](auto) { callback_triggered = true; });
  RunLoopUntilIdle();

  EXPECT_TRUE(callback_triggered);
  EXPECT_TRUE(received_responses_.empty());
}

// Tests that an EndContest() call in |respond| doesn't cause ASAN issues on destruction.
TEST_F(TouchSourceTest, ReentryOnDestruction_ShouldNotCauseUseAfterFreeErrors) {
  bool respond_called = false;
  touch_source_.emplace(
      client_ptr_.NewRequest(),
      /*respond*/
      [this, &respond_called](StreamId stream_id, const std::vector<GestureResponse>& responses) {
        respond_called = true;
        touch_source_->EndContest(stream_id, /*awarded_win*/ false);
      },
      /*error_handler*/ [] {});

  touch_source_->UpdateStream(
      kStreamId, {.device_id = kDeviceId, .pointer_id = kPointerId, .phase = Phase::kAdd}, false);

  EXPECT_FALSE(respond_called);
  touch_source_.reset();
  EXPECT_TRUE(respond_called);
}

}  // namespace
}  // namespace lib_ui_input_tests
