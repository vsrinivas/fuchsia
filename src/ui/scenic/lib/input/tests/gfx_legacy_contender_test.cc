// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/gfx_legacy_contender.h"

#include <gtest/gtest.h>

namespace lib_ui_input_tests {
namespace {

using scenic_impl::input::GestureResponse;
using scenic_impl::input::GfxLegacyContender;
using scenic_impl::input::InternalPointerEvent;
using scenic_impl::input::StreamId;

constexpr StreamId kStreamId = 1;

TEST(GfxLegacyContenderTest, ShouldGetYESResponseForEachMessage) {
  uint64_t num_responses = 0;
  auto contender = GfxLegacyContender(
      /*respond*/
      [&num_responses](GestureResponse response) {
        ++num_responses;
        EXPECT_EQ(response, GestureResponse::kYes);
      },
      /*deliver_events_to_client*/ [](auto) {},
      /*self_destruct*/ [] {});

  EXPECT_EQ(num_responses, 0u);
  contender.UpdateStream(kStreamId, /*event*/ {}, /*is_end_of_stream*/ false);
  EXPECT_EQ(num_responses, 1u);
  contender.UpdateStream(kStreamId, /*event*/ {}, /*is_end_of_stream*/ false);
  EXPECT_EQ(num_responses, 2u);
  contender.UpdateStream(kStreamId, /*event*/ {}, /*is_end_of_stream*/ true);
  EXPECT_EQ(num_responses, 3u);
}

TEST(GfxLegacyContenderTest, ShouldGetAllEventsOnWin) {
  constexpr StreamId kStreamId = 1;
  std::vector<InternalPointerEvent> last_delivered_events;
  auto contender = GfxLegacyContender(
      /*respond*/ [](auto) {}, /*deliver_events_to_client*/
      [&last_delivered_events](const std::vector<InternalPointerEvent>& events) {
        last_delivered_events = events;
      },
      /*self_destruct*/ [] {});

  // No events delivered before being awarded a win.
  contender.UpdateStream(kStreamId, /*event*/ {.timestamp = 0}, /*is_end_of_stream*/ false);
  EXPECT_TRUE(last_delivered_events.empty());
  contender.UpdateStream(kStreamId, /*event*/ {.timestamp = 1}, /*is_end_of_stream*/ false);
  EXPECT_TRUE(last_delivered_events.empty());

  // All previous events should be delivered on win.
  contender.EndContest(kStreamId, /*awarded_win*/ true);
  ASSERT_EQ(last_delivered_events.size(), 2u);
  EXPECT_EQ(last_delivered_events[0].timestamp, 0);
  EXPECT_EQ(last_delivered_events[1].timestamp, 1);

  // Subsequent events are delivered immediately.
  contender.UpdateStream(kStreamId, /*event*/ {.timestamp = 2}, /*is_end_of_stream*/ false);
  ASSERT_EQ(last_delivered_events.size(), 1u);
  EXPECT_EQ(last_delivered_events[0].timestamp, 2);
}

TEST(GfxLegacyContenderTest, ShouldSelfDestructOnLoss) {
  bool deliver_called = false;
  bool self_destruct_called = false;
  auto contender = GfxLegacyContender(
      /*respond*/ [](auto) {},
      /*deliver_events_to_client*/ [&deliver_called](auto) { deliver_called = true; },
      /*self_destruct*/ [&self_destruct_called] { self_destruct_called = true; });

  contender.UpdateStream(kStreamId, /*event*/ {.timestamp = 0}, /*is_end_of_stream*/ false);
  EXPECT_FALSE(deliver_called);
  EXPECT_FALSE(self_destruct_called);

  // Should self-destruct on loss.
  contender.EndContest(kStreamId, /*awarded_win*/ false);
  EXPECT_FALSE(deliver_called);
  EXPECT_TRUE(self_destruct_called);
}

TEST(GfxLegacyContenderTest, ShouldSelfDescructOnStreamEndAfterWin) {
  uint64_t num_delivered_events = 0;

  bool self_destruct_called = false;
  auto contender = GfxLegacyContender(
      /*respond*/ [](auto) {},
      /*deliver_events_to_client*/
      [&num_delivered_events](auto events) { num_delivered_events += events.size(); },
      /*self_destruct*/ [&self_destruct_called] { self_destruct_called = true; });

  contender.UpdateStream(kStreamId, /*event*/ {.timestamp = 0}, /*is_end_of_stream*/ false);
  EXPECT_EQ(num_delivered_events, 0u);
  EXPECT_FALSE(self_destruct_called);

  // Win the contest. Deliver events.
  contender.EndContest(kStreamId, /*awarded_win*/ true);
  EXPECT_EQ(num_delivered_events, 1u);
  EXPECT_FALSE(self_destruct_called);

  // No destruction while stream is ongoing.
  contender.UpdateStream(kStreamId, /*event*/ {.timestamp = 0}, /*is_end_of_stream*/ false);
  EXPECT_EQ(num_delivered_events, 2u);
  EXPECT_FALSE(self_destruct_called);

  // Deliver the last event and then self destruct on stream end.
  contender.UpdateStream(kStreamId, /*event*/ {.timestamp = 0}, /*is_end_of_stream*/ true);
  EXPECT_EQ(num_delivered_events, 3u);
  EXPECT_TRUE(self_destruct_called);
}

TEST(GfxLegacyContenderTest, ShouldSelfDescructOnWinAfterStreamEnd) {
  uint64_t num_delivered_events = 0;

  bool self_destruct_called = false;
  auto contender = GfxLegacyContender(
      /*respond*/ [](auto) {},
      /*deliver_events_to_client*/
      [&num_delivered_events](auto events) { num_delivered_events += events.size(); },
      /*self_destruct*/ [&self_destruct_called] { self_destruct_called = true; });

  contender.UpdateStream(kStreamId, /*event*/ {.timestamp = 0}, /*is_end_of_stream*/ true);
  EXPECT_EQ(num_delivered_events, 0u);
  EXPECT_FALSE(self_destruct_called);

  // Win the contest. Deliver events.
  contender.EndContest(kStreamId, /*awarded_win*/ true);
  EXPECT_EQ(num_delivered_events, 1u);
  EXPECT_TRUE(self_destruct_called);
}

}  // namespace
}  // namespace lib_ui_input_tests
