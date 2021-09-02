// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/mouse_source.h"

#include <lib/async-testing/test_loop.h>
#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lib/gtest/test_loop_fixture.h"

namespace input::test {

using fuchsia::ui::pointer::MouseViewStatus;
using scenic_impl::input::Extents;
using scenic_impl::input::InternalMouseEvent;
using scenic_impl::input::MouseSource;
using scenic_impl::input::ScrollInfo;
using scenic_impl::input::StreamId;
using scenic_impl::input::Viewport;

constexpr StreamId kStreamId = 1;
constexpr uint32_t kDeviceId = 2;

constexpr view_tree::BoundingBox kEmptyBoundingBox{};

constexpr bool kInsideView = false;
constexpr bool kExitView = true;

namespace {

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

InternalMouseEvent IMEventTemplate() {
  return {
      .device_id = kDeviceId,
      .viewport =
          {
              .receiver_from_viewport_transform = std::array<float, 9>(),
          },
  };
}

}  // namespace

class MouseSourceTest : public gtest::TestLoopFixture {
 protected:
  void SetUp() override {
    client_ptr_.set_error_handler([this](auto) { channel_closed_ = true; });

    mouse_source_.emplace(client_ptr_.NewRequest(),
                          /*error_handler*/ [this] { internal_error_handler_fired_ = true; });
  }

  bool internal_error_handler_fired_ = false;
  bool channel_closed_ = false;

  fuchsia::ui::pointer::MouseSourcePtr client_ptr_;
  std::optional<MouseSource> mouse_source_;
};

TEST_F(MouseSourceTest, Watch_WithNoPendingMessages_ShouldNeverReturn) {
  bool callback_triggered = false;
  client_ptr_->Watch([&callback_triggered](auto) { callback_triggered = true; });

  RunLoopUntilIdle();
  EXPECT_FALSE(channel_closed_);
  EXPECT_FALSE(callback_triggered);
}

TEST_F(MouseSourceTest, ErrorHandler_ShouldFire_OnClientDisconnect) {
  EXPECT_FALSE(internal_error_handler_fired_);
  client_ptr_.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(internal_error_handler_fired_);
}

TEST_F(MouseSourceTest, Watch_CallingTwiceWithoutWaiting_ShouldCloseChannel) {
  client_ptr_->Watch([](auto) { EXPECT_FALSE(true); });
  client_ptr_->Watch([](auto) { EXPECT_FALSE(true); });
  RunLoopUntilIdle();
  EXPECT_TRUE(channel_closed_);
}

TEST_F(MouseSourceTest, Watch_BeforeEvents_ShouldReturnOnFirstEvent) {
  uint64_t num_events = 0;
  client_ptr_->Watch([&num_events](auto events) { num_events += events.size(); });

  RunLoopUntilIdle();
  EXPECT_FALSE(channel_closed_);
  EXPECT_EQ(num_events, 0u);

  // Sending fidl message on first event, so expect the second one not to arrive.
  mouse_source_->UpdateStream(kStreamId, IMEventTemplate(), kEmptyBoundingBox, kInsideView);
  mouse_source_->UpdateStream(kStreamId, IMEventTemplate(), kEmptyBoundingBox, kInsideView);

  RunLoopUntilIdle();
  EXPECT_FALSE(channel_closed_);
  EXPECT_EQ(num_events, 1u);

  // Second event should arrive on next Watch() call.
  client_ptr_->Watch([&num_events](auto events) { num_events += events.size(); });
  RunLoopUntilIdle();
  EXPECT_FALSE(channel_closed_);
  EXPECT_EQ(num_events, 2u);
}

TEST_F(MouseSourceTest, Watch_ShouldAtMostReturn_MOUSE_MAX_EVENT_Events_PerCall) {
  // Sending fidl message on first event, so expect the second one not to arrive.
  mouse_source_->UpdateStream(kStreamId, IMEventTemplate(), kEmptyBoundingBox, kInsideView);
  for (size_t i = 0; i < fuchsia::ui::pointer::MOUSE_MAX_EVENT + 3; ++i) {
    mouse_source_->UpdateStream(kStreamId, IMEventTemplate(), kEmptyBoundingBox, kInsideView);
  }

  client_ptr_->Watch(
      [](auto events) { ASSERT_EQ(events.size(), fuchsia::ui::pointer::MOUSE_MAX_EVENT); });
  RunLoopUntilIdle();

  // The 4 events remaining in the queue should be delivered with the next Watch() call.
  client_ptr_->Watch([](auto events) { EXPECT_EQ(events.size(), 4u); });
  RunLoopUntilIdle();
}

TEST_F(MouseSourceTest, ViewportIsDeliveredCorrectly) {
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

  Viewport viewport2;
  viewport2.extents = std::array<std::array<float, 2>, 2>{{{-5, 1}, {100, 40}}};
  viewport2.receiver_from_viewport_transform = {
      // clang-format off
     1,  2,  3, // column one
     4,  5,  6, // column two
     7,  8,  9  // column three
      // clang-format on
  };
  const view_tree::BoundingBox view_bounds2{.min = {-1, -2}, .max = {3, 4}};

  {
    auto event = IMEventTemplate();
    event.viewport = viewport1;
    mouse_source_->UpdateStream(kStreamId, event, view_bounds1, kInsideView);
  }
  {
    auto event = IMEventTemplate();
    event.viewport = viewport1;
    mouse_source_->UpdateStream(kStreamId, event, view_bounds1, kInsideView);
  }
  {
    auto event = IMEventTemplate();
    event.viewport = viewport2;
    mouse_source_->UpdateStream(kStreamId, event, view_bounds2, kInsideView);
  }

  client_ptr_->Watch([&](auto events) {
    ASSERT_EQ(events.size(), 3u);
    EXPECT_TRUE(events[0].has_pointer_sample());
    ASSERT_TRUE(events[0].has_view_parameters());
    ExpectEqual(events[0].view_parameters(), viewport1, view_bounds1);

    EXPECT_FALSE(events[1].has_view_parameters());
    EXPECT_TRUE(events[1].has_pointer_sample());

    EXPECT_TRUE(events[2].has_pointer_sample());
    ASSERT_TRUE(events[2].has_view_parameters());
    ExpectEqual(events[2].view_parameters(), viewport2, view_bounds2);
  });

  RunLoopUntilIdle();
}

// Struct for tracking

TEST_F(MouseSourceTest, MouseDeviceInfo_ShouldBeSent_OncePerDevice) {
  const uint32_t kDeviceId1 = 11111, kDeviceId2 = 22222;

  // Start three separate streams, two with the kDeviceId1 and one with kDeviceId2.
  {
    InternalMouseEvent event = IMEventTemplate();
    event.device_id = kDeviceId1;
    event.buttons = {.identifiers = {12, 34, 56}};
    event.scroll_v = {
        .unit = fuchsia::input::report::UnitType::DEGREES,
        .exponent = 900,
        .range = {-98, 76},
    };
    mouse_source_->UpdateStream(/*stream_id*/ 1, event, kEmptyBoundingBox, kInsideView);
  }
  {
    InternalMouseEvent event = IMEventTemplate();
    event.device_id = kDeviceId1;
    event.buttons = {.pressed = {12, 56}};
    mouse_source_->UpdateStream(/*stream_id*/ 2, event, kEmptyBoundingBox, kInsideView);
  }
  {
    InternalMouseEvent event = IMEventTemplate();
    event.device_id = kDeviceId2;
    event.scroll_h = {
        .unit = fuchsia::input::report::UnitType::METERS,
        .exponent = -111,
        .range = {100, 200},
    };
    mouse_source_->UpdateStream(/*stream_id*/ 3, event, kEmptyBoundingBox, kInsideView);
  }
  RunLoopUntilIdle();

  {  // Only the first instance of each device_id should generate a device_info parameter.
    std::vector<fuchsia::ui::pointer::MouseEvent> received_events;
    client_ptr_->Watch([&received_events](auto events) { received_events = std::move(events); });
    RunLoopUntilIdle();

    ASSERT_EQ(received_events.size(), 3u);

    {
      const auto& event = received_events[0];

      ASSERT_TRUE(event.has_device_info());
      const auto& device_info = event.device_info();
      EXPECT_EQ(device_info.id(), kDeviceId1);
      ASSERT_TRUE(device_info.has_scroll_v_range());
      const auto& [range, unit] = device_info.scroll_v_range();
      EXPECT_EQ(range.min, -98);
      EXPECT_EQ(range.max, 76);
      EXPECT_EQ(unit.type, fuchsia::input::report::UnitType::DEGREES);
      EXPECT_EQ(unit.exponent, 900);
      EXPECT_FALSE(device_info.has_scroll_h_range());
      ASSERT_TRUE(device_info.has_buttons());
      EXPECT_THAT(device_info.buttons(), testing::ElementsAre(12, 34, 56));

      ASSERT_TRUE(event.has_pointer_sample());
      ASSERT_TRUE(event.pointer_sample().has_device_id());
      EXPECT_EQ(event.pointer_sample().device_id(), kDeviceId1);
    }

    {
      const auto& event = received_events[1];
      EXPECT_FALSE(event.has_device_info());
      ASSERT_TRUE(event.has_pointer_sample());
      const auto& pointer_sample = event.pointer_sample();
      ASSERT_TRUE(pointer_sample.has_device_id());
      EXPECT_EQ(pointer_sample.device_id(), kDeviceId1);
      ASSERT_TRUE(pointer_sample.has_pressed_buttons());
      EXPECT_THAT(pointer_sample.pressed_buttons(), testing::ElementsAre(12, 56));
    }

    {
      const auto& event = received_events[2];

      ASSERT_TRUE(event.has_device_info());
      const auto& device_info = event.device_info();
      EXPECT_EQ(device_info.id(), kDeviceId2);
      EXPECT_FALSE(device_info.has_scroll_v_range());
      ASSERT_TRUE(device_info.has_scroll_h_range());
      const auto& [range, unit] = device_info.scroll_h_range();
      EXPECT_EQ(range.min, 100);
      EXPECT_EQ(range.max, 200);
      EXPECT_EQ(unit.type, fuchsia::input::report::UnitType::METERS);
      EXPECT_EQ(unit.exponent, -111);
      EXPECT_FALSE(device_info.has_buttons());

      ASSERT_TRUE(event.has_pointer_sample());
      ASSERT_TRUE(event.pointer_sample().has_device_id());
      EXPECT_EQ(event.pointer_sample().device_id(), kDeviceId2);
    }
  }
}

TEST_F(MouseSourceTest, FullStreamTest) {
  mouse_source_->UpdateStream(kStreamId, IMEventTemplate(), kEmptyBoundingBox, kInsideView);
  mouse_source_->UpdateStream(kStreamId, IMEventTemplate(), kEmptyBoundingBox, kInsideView);
  // Exit view.
  mouse_source_->UpdateStream(kStreamId, IMEventTemplate(), kEmptyBoundingBox, kExitView);
  // Re-enter view.
  mouse_source_->UpdateStream(kStreamId, IMEventTemplate(), kEmptyBoundingBox, kInsideView);
  mouse_source_->UpdateStream(kStreamId, IMEventTemplate(), kEmptyBoundingBox, kInsideView);

  client_ptr_->Watch([&](auto events) {
    ASSERT_EQ(events.size(), 5u);
    {
      const auto& event = events[0];
      EXPECT_TRUE(event.has_timestamp());
      ASSERT_TRUE(event.has_pointer_sample());
      EXPECT_TRUE(event.has_view_parameters());
      EXPECT_TRUE(event.has_device_info());
      ASSERT_TRUE(event.has_stream_info());
      EXPECT_TRUE(event.has_trace_flow_id());

      EXPECT_EQ(event.stream_info().status, MouseViewStatus::ENTERED);
    }

    {
      const auto& event = events[1];
      EXPECT_TRUE(event.has_timestamp());
      ASSERT_TRUE(event.has_pointer_sample());
      EXPECT_FALSE(event.has_view_parameters());
      EXPECT_FALSE(event.has_device_info());
      EXPECT_FALSE(event.has_stream_info());
      EXPECT_TRUE(event.has_trace_flow_id());
    }

    {  // Exit view
      const auto& event = events[2];
      EXPECT_TRUE(event.has_timestamp());
      EXPECT_FALSE(event.has_pointer_sample());
      EXPECT_FALSE(event.has_view_parameters());
      EXPECT_FALSE(event.has_device_info());
      ASSERT_TRUE(event.has_stream_info());
      EXPECT_TRUE(event.has_trace_flow_id());

      EXPECT_EQ(event.stream_info().status, MouseViewStatus::EXITED);
    }

    {  // Re-enter view.
      const auto& event = events[3];
      EXPECT_TRUE(event.has_timestamp());
      ASSERT_TRUE(event.has_pointer_sample());
      EXPECT_FALSE(event.has_view_parameters());
      EXPECT_FALSE(event.has_device_info());
      ASSERT_TRUE(event.has_stream_info());
      EXPECT_TRUE(event.has_trace_flow_id());

      EXPECT_EQ(event.stream_info().status, MouseViewStatus::ENTERED);
    }

    {
      const auto& event = events[4];
      EXPECT_TRUE(event.has_timestamp());
      ASSERT_TRUE(event.has_pointer_sample());
      EXPECT_FALSE(event.has_view_parameters());
      EXPECT_FALSE(event.has_device_info());
      EXPECT_FALSE(event.has_stream_info());
      EXPECT_TRUE(event.has_trace_flow_id());
    }
  });

  RunLoopUntilIdle();
}

}  // namespace input::test
