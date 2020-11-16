// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/a11y_legacy_contender.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace lib_ui_input_tests {
namespace {

using scenic_impl::input::A11yLegacyContender;
using scenic_impl::input::GestureResponse;
using scenic_impl::input::InternalPointerEvent;
using scenic_impl::input::StreamId;

TEST(A11yLegacyContenderTest, SingleStream_ConsumedAtSweep) {
  constexpr StreamId kId1 = 1;
  constexpr uint32_t kPointerId1 = 4;
  std::vector<GestureResponse> responses;
  std::vector<InternalPointerEvent> events_sent_to_client;
  auto contender = A11yLegacyContender(
      /*respond*/
      [&responses](StreamId id, GestureResponse response) { responses.push_back(response); },
      /*deliver_events_to_client*/
      [&events_sent_to_client](const InternalPointerEvent& event) {
        events_sent_to_client.emplace_back(event);
      });

  // Start a stream. No events shuld get responses until the client makes a decision,
  // and all events should be forwarded to client.
  EXPECT_EQ(events_sent_to_client.size(), 0u);
  contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId1}, /*is_end_of_stream*/ false);
  EXPECT_EQ(events_sent_to_client.size(), 1u);
  EXPECT_TRUE(responses.empty());
  contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId1}, /*is_end_of_stream*/ false);
  EXPECT_EQ(events_sent_to_client.size(), 2u);
  EXPECT_TRUE(responses.empty());
  contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId1}, /*is_end_of_stream*/ true);
  EXPECT_EQ(events_sent_to_client.size(), 3u);
  EXPECT_TRUE(responses.empty());

  contender.OnStreamHandled(kPointerId1,
                            fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  ASSERT_EQ(responses.size(), 3u);
  EXPECT_THAT(responses, testing::Each(GestureResponse::kYesPrioritize));

  // Award the win. Expect no more responses.
  responses.clear();
  events_sent_to_client.clear();
  contender.EndContest(kId1, /*awarded_win*/ true);
  EXPECT_TRUE(events_sent_to_client.empty());
  EXPECT_TRUE(responses.empty());
}

TEST(A11yLegacyContenderTest, SingleStream_ConsumedMidContest) {
  constexpr StreamId kId1 = 1;
  constexpr uint32_t kPointerId1 = 4;
  std::vector<GestureResponse> responses;
  std::vector<InternalPointerEvent> events_sent_to_client;
  auto contender = A11yLegacyContender(
      /*respond*/
      [&responses](StreamId id, GestureResponse response) { responses.push_back(response); },
      /*deliver_events_to_client*/
      [&events_sent_to_client](const InternalPointerEvent& event) {
        events_sent_to_client.emplace_back(event);
      });

  // Start a stream. No events should get responses until the client makes a decision,
  // and all events should be forwarded to client.
  EXPECT_EQ(events_sent_to_client.size(), 0u);
  contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId1}, /*is_end_of_stream*/ false);
  contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId1}, /*is_end_of_stream*/ false);
  EXPECT_EQ(events_sent_to_client.size(), 2u);
  EXPECT_TRUE(responses.empty());

  // Since the stream hasn't ended yet we're not at sweep, but the YES_PRIORITIZE response is sent
  // immediately.
  contender.OnStreamHandled(kPointerId1,
                            fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  ASSERT_EQ(responses.size(), 2u);
  EXPECT_THAT(responses, testing::Each(GestureResponse::kYesPrioritize));

  // Subsequent events should have a YES response.
  contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId1}, /*is_end_of_stream*/ false);
  ASSERT_EQ(responses.size(), 3u);
  EXPECT_EQ(responses[2], GestureResponse::kYesPrioritize);

  // Award the win. Expect no responses on subsequent events.
  responses.clear();
  events_sent_to_client.clear();
  contender.EndContest(kId1, /*awarded_win*/ true);
  contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId1}, /*is_end_of_stream*/ false);
  contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId1}, /*is_end_of_stream*/ true);
  EXPECT_EQ(events_sent_to_client.size(), 2u);
  EXPECT_TRUE(responses.empty());
}

TEST(A11yLegacyContenderTest, SingleStream_Rejected) {
  constexpr StreamId kId1 = 1;
  constexpr uint32_t kPointerId1 = 4;
  std::vector<GestureResponse> responses;
  std::vector<InternalPointerEvent> events_sent_to_client;
  auto contender = A11yLegacyContender(
      /*respond*/
      [&responses](StreamId id, GestureResponse response) { responses.push_back(response); },
      /*deliver_events_to_client*/
      [&events_sent_to_client](const InternalPointerEvent& event) {
        events_sent_to_client.emplace_back(event);
      });

  contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId1}, /*is_end_of_stream*/ false);
  contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId1}, /*is_end_of_stream*/ false);
  EXPECT_EQ(events_sent_to_client.size(), 2u);
  EXPECT_TRUE(responses.empty());

  // On rejection we should get single NO response.
  contender.OnStreamHandled(kPointerId1,
                            fuchsia::ui::input::accessibility::EventHandling::REJECTED);
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0], GestureResponse::kNo);
}

// Tests that no further responses are sent after the contest ends.
TEST(A11yLegacyContenderTest, ContestEndedOnResponse) {
  constexpr StreamId kId1 = 1;
  constexpr uint32_t kPointerId1 = 4;
  std::vector<GestureResponse> responses;
  std::vector<InternalPointerEvent> events_sent_to_client;
  A11yLegacyContender* contender_ptr;
  auto contender = A11yLegacyContender(
      /*respond*/
      [&responses, &contender_ptr](StreamId id, GestureResponse response) {
        responses.push_back(response);
        contender_ptr->EndContest(id, /*awarded_win*/ true);
      },
      /*deliver_events_to_client*/
      [&events_sent_to_client](const InternalPointerEvent& event) {
        events_sent_to_client.emplace_back(event);
      });
  contender_ptr = &contender;

  contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId1}, /*is_end_of_stream*/ false);
  contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId1}, /*is_end_of_stream*/ false);
  contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId1}, /*is_end_of_stream*/ false);
  EXPECT_EQ(events_sent_to_client.size(), 3u);
  EXPECT_TRUE(responses.empty());

  // Consume the stream. The win is awarded on the first response, and no further responses
  // should be seen.
  contender.OnStreamHandled(kPointerId1,
                            fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0], GestureResponse::kYesPrioritize);

  // Check that events are delivered after contest end.
  events_sent_to_client.clear();
  contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId1}, /*is_end_of_stream*/ false);
  EXPECT_EQ(events_sent_to_client.size(), 1u);
}

TEST(A11yLegacyContenderTest, MultipleStreams) {
  constexpr StreamId kId1 = 1, kId2 = 2, kId3 = 3;
  constexpr uint32_t kPointerId1 = 4, kPointerId2 = 5, kPointerId3 = 6;
  std::unordered_map<StreamId, std::vector<GestureResponse>> responses;
  std::vector<InternalPointerEvent> events_sent_to_client;
  auto contender = A11yLegacyContender(
      /*respond*/
      [&responses](StreamId id, GestureResponse response) { responses[id].push_back(response); },
      /*deliver_events_to_client*/
      [&events_sent_to_client](const InternalPointerEvent& event) {
        events_sent_to_client.emplace_back(event);
      });

  // Start three streams and make sure they're all handled correctly individually.
  EXPECT_EQ(events_sent_to_client.size(), 0u);
  contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId1}, /*is_end_of_stream*/ false);
  contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId1}, /*is_end_of_stream*/ false);
  EXPECT_EQ(events_sent_to_client.size(), 2u);
  EXPECT_TRUE(responses.empty());

  contender.UpdateStream(kId2, /*event*/ {.pointer_id = kPointerId2}, /*is_end_of_stream*/ false);
  contender.UpdateStream(kId2, /*event*/ {.pointer_id = kPointerId2}, /*is_end_of_stream*/ true);
  EXPECT_EQ(events_sent_to_client.size(), 4u);
  EXPECT_TRUE(responses.empty());

  contender.UpdateStream(kId3, /*event*/ {.pointer_id = kPointerId3}, /*is_end_of_stream*/ false);
  contender.UpdateStream(kId3, /*event*/ {.pointer_id = kPointerId3}, /*is_end_of_stream*/ false);
  EXPECT_EQ(events_sent_to_client.size(), 6u);
  EXPECT_TRUE(responses.empty());

  // Now the client decides on all three streams and observe the expected responses.
  events_sent_to_client.clear();
  contender.OnStreamHandled(kPointerId1,
                            fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_EQ(responses.size(), 1u);
  ASSERT_EQ(responses[kId1].size(), 2u);
  EXPECT_THAT(responses[kId1], testing::Each(GestureResponse::kYesPrioritize));
  contender.OnStreamHandled(kPointerId2,
                            fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_EQ(responses.size(), 2u);
  ASSERT_EQ(responses[kId2].size(), 2u);
  EXPECT_THAT(responses[kId2], testing::Each(GestureResponse::kYesPrioritize));
  contender.OnStreamHandled(kPointerId3,
                            fuchsia::ui::input::accessibility::EventHandling::REJECTED);
  EXPECT_EQ(responses.size(), 3u);
  ASSERT_EQ(responses[kId3].size(), 1u);
  EXPECT_EQ(responses[kId3][0], GestureResponse::kNo);

  EXPECT_EQ(events_sent_to_client.size(), 0u);

  // End contests 2 and 3.
  contender.EndContest(kId2, /*awarded_win*/ true);
  contender.EndContest(kId3, /*awarded_win*/ false);
  responses.clear();

  // Since streams 2 and 3 ended and lost respectively they should count as new streams if used
  // again.
  contender.UpdateStream(kId2, /*event*/ {.pointer_id = kPointerId2}, /*is_end_of_stream*/ false);
  EXPECT_EQ(events_sent_to_client.size(), 1u);
  EXPECT_TRUE(responses.empty());
  contender.UpdateStream(kId3, /*event*/ {.pointer_id = kPointerId3}, /*is_end_of_stream*/ false);
  EXPECT_EQ(events_sent_to_client.size(), 2u);
  EXPECT_TRUE(responses.empty());

  // Stream 1 should continue to receive YES_PRIORITIZE on each new message, since that stream is
  // still ongoing.
  contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId1}, /*is_end_of_stream*/ false);
  EXPECT_EQ(events_sent_to_client.size(), 3u);
  EXPECT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[kId1][0], GestureResponse::kYesPrioritize);
}

// This test ensures that the contender can handle receiving multiple streams with the same
// pointer_id before a11y has time to respond.
TEST(A11yLegacyContenderTest, MultipleStreams_WithSamePointer) {
  constexpr StreamId kId1 = 1, kId2 = 2, kId3 = 3;
  constexpr uint32_t kPointerId = 4;
  std::unordered_map<StreamId, std::vector<GestureResponse>> responses;
  auto contender = A11yLegacyContender(
      /*respond*/
      [&responses](StreamId id, GestureResponse response) { responses[id].push_back(response); },
      /*deliver_events_to_client*/
      [](auto) {});

  // Create three streams and end them.
  contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId}, /*is_end_of_stream*/ true);
  contender.UpdateStream(kId2, /*event*/ {.pointer_id = kPointerId}, /*is_end_of_stream*/ true);
  contender.UpdateStream(kId3, /*event*/ {.pointer_id = kPointerId}, /*is_end_of_stream*/ true);
  EXPECT_TRUE(responses.empty());

  // Return OnStreamHandled messages for all ongoing streams, but always reuse kPointerId.
  // Observe that each stream gets the correct message.
  contender.OnStreamHandled(kPointerId, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_EQ(responses.size(), 1u);
  ASSERT_EQ(responses[kId1].size(), 1u);
  EXPECT_EQ(responses[kId1][0], GestureResponse::kYesPrioritize);
  contender.OnStreamHandled(kPointerId, fuchsia::ui::input::accessibility::EventHandling::REJECTED);
  EXPECT_EQ(responses.size(), 2u);
  ASSERT_EQ(responses[kId2].size(), 1u);
  EXPECT_EQ(responses[kId2][0], GestureResponse::kNo);
  contender.OnStreamHandled(kPointerId, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_EQ(responses.size(), 3u);
  ASSERT_EQ(responses[kId3].size(), 1u);
  EXPECT_EQ(responses[kId3][0], GestureResponse::kYesPrioritize);
}

// Check that all ongoing streams (streams that either haven't been decided, or that were
// won but haven't ended yet) receive a NO response on destruction.
TEST(A11yLegacyContenderTest, EndOngoingStreamsOnDestruction) {
  constexpr StreamId kId1 = 1, kId2 = 2, kId3 = 3, kId4 = 4, kId5 = 5, kId6 = 6;
  constexpr uint32_t kPointerId1 = 6, kPointerId2 = 7, kPointerId3 = 8, kPointerId4 = 9,
                     kPointerId5 = 10, kPointerId6 = 11;
  std::unordered_map<StreamId, std::vector<GestureResponse>> responses;

  {
    auto contender = A11yLegacyContender(
        /*respond*/
        [&responses](StreamId id, GestureResponse response) { responses[id].push_back(response); },
        /*deliver_events_to_client*/
        [](auto) {});

    // Starting 6 streams to test all combinations that cause ongoing or ended streams.

    // Ended.
    contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId1}, /*is_end_of_stream*/ true);
    contender.EndContest(kId1, /*awarded_win*/ true);

    // Ended.
    contender.UpdateStream(kId2, /*event*/ {.pointer_id = kPointerId2}, /*is_end_of_stream*/ true);
    contender.EndContest(kId2, /*awarded_win*/ false);

    // Ongoing.
    contender.UpdateStream(kId3, /*event*/ {.pointer_id = kPointerId3}, /*is_end_of_stream*/ false);
    contender.EndContest(kId3, /*awarded_win*/ true);

    // Ended.
    contender.UpdateStream(kId4, /*event*/ {.pointer_id = kPointerId4}, /*is_end_of_stream*/ false);
    contender.EndContest(kId4, /*awarded_win*/ false);

    // Ongoing.
    contender.UpdateStream(kId5, /*event*/ {.pointer_id = kPointerId5}, /*is_end_of_stream*/ false);

    // Stream ended, contest ongoing.
    contender.UpdateStream(kId6, /*event*/ {.pointer_id = kPointerId6}, /*is_end_of_stream*/ false);

    responses.clear();
  }

  // As |contender| goes out of scope, ongoing streams should receive a NO response.
  EXPECT_EQ(responses.size(), 3u);
  ASSERT_EQ(responses[kId3].size(), 1u);
  EXPECT_EQ(responses[kId3][0], GestureResponse::kNo);
  ASSERT_EQ(responses[kId5].size(), 1u);
  EXPECT_EQ(responses[kId5][0], GestureResponse::kNo);
  ASSERT_EQ(responses[kId6].size(), 1u);
  EXPECT_EQ(responses[kId6][0], GestureResponse::kNo);
}

// Tests that contests ending out of order is cleaned up correctly.
TEST(A11yLegacyContenderTest, ContestsEndingOutOfOrder) {
  constexpr StreamId kId1 = 1, kId2 = 2, kId3 = 3;
  constexpr uint32_t kPointerId = 4;
  std::unordered_map<StreamId, std::vector<GestureResponse>> responses;

  {
    auto contender = A11yLegacyContender(
        /*respond*/
        [&responses](StreamId id, GestureResponse response) { responses[id].push_back(response); },
        /*deliver_events_to_client*/
        [](auto) {});

    // Start three streams for the same pointer.
    contender.UpdateStream(kId1, /*event*/ {.pointer_id = kPointerId}, /*is_end_of_stream*/ false);
    contender.UpdateStream(kId2, /*event*/ {.pointer_id = kPointerId}, /*is_end_of_stream*/ false);
    contender.UpdateStream(kId3, /*event*/ {.pointer_id = kPointerId}, /*is_end_of_stream*/ false);

    // Now end the second contest with a loss before any responses have been sent.
    contender.EndContest(kId2, /*awarded_win*/ false);
  }

  // As |contender| goes out of scope, the two still ongoing streams should receive a NO response.
  EXPECT_EQ(responses.size(), 2u);
  ASSERT_EQ(responses[kId1].size(), 1u);
  EXPECT_EQ(responses[kId1][0], GestureResponse::kNo);
  ASSERT_EQ(responses[kId3].size(), 1u);
  EXPECT_EQ(responses[kId3][0], GestureResponse::kNo);
}

}  // namespace
}  // namespace lib_ui_input_tests
