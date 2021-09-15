// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testing/test_loop.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lib/gtest/test_loop_fixture.h"
#include "src/ui/scenic/lib/input/input_system.h"
#include "src/ui/scenic/lib/input/mouse_source.h"

// These tests exercise the full mouse delivery flow of InputSystem for
// clients of the fuchsia.ui.pointer.MouseSource protocol.

namespace input::test {

using fup_GlobalMouseEvent = fuchsia::ui::pointer::augment::MouseEventWithGlobalMouse;
using fup_MouseEvent = fuchsia::ui::pointer::MouseEvent;
using fuchsia::ui::pointer::MouseViewStatus;

using scenic_impl::input::InternalMouseEvent;
using scenic_impl::input::MouseSource;
using scenic_impl::input::StreamId;

constexpr zx_koid_t kContextKoid = 100u;
constexpr zx_koid_t kClient1Koid = 1u;
constexpr zx_koid_t kClient2Koid = 2u;
constexpr zx_koid_t kClient3Koid = 3u;

constexpr StreamId kStream1Id = 11u;
constexpr StreamId kStream2Id = 22u;

constexpr uint32_t kButtonId = 33u;

namespace {

InternalMouseEvent MouseEventTemplate(zx_koid_t target, bool button_down = false) {
  InternalMouseEvent event{.timestamp = 0,
                           .device_id = 1u,
                           .context = kContextKoid,
                           .target = target,
                           .position_in_viewport = glm::vec2(5, 5),  // Middle of viewport.
                           .buttons = {
                               .identifiers = {kButtonId},
                           }};

  if (button_down) {
    event.buttons.pressed = {kButtonId};
  }

  event.viewport.extents.min = {0, 0};
  event.viewport.extents.max = {10, 10};
  return event;
}

// Creates a new snapshot with a hit test that returns |hits|, and a ViewTree with a straight
// hierarchy matching |hierarchy|.
std::shared_ptr<view_tree::Snapshot> NewSnapshot(std::vector<zx_koid_t> hits,
                                                 std::vector<zx_koid_t> hierarchy) {
  auto snapshot = std::make_shared<view_tree::Snapshot>();

  if (!hierarchy.empty()) {
    snapshot->root = hierarchy[0];
    const auto [_, success] = snapshot->view_tree.try_emplace(hierarchy[0]);
    FX_DCHECK(success);
    if (hierarchy.size() > 1) {
      snapshot->view_tree[hierarchy[0]].children = {hierarchy[1]};
      for (size_t i = 1; i < hierarchy.size() - 1; ++i) {
        snapshot->view_tree[hierarchy[i]].parent = hierarchy[i - 1];
        snapshot->view_tree[hierarchy[i]].children = {hierarchy[i + 1]};
      }
      snapshot->view_tree[hierarchy.back()].parent = hierarchy[hierarchy.size() - 2];
    }
  }

  snapshot->hit_testers.emplace_back(
      [hits](auto...) { return view_tree::SubtreeHitTestResult{.hits = hits}; });

  return snapshot;
}

}  // namespace

class MouseTest : public gtest::TestLoopFixture {
 public:
  MouseTest()
      : input_system_(
            scenic_impl::SystemContext(context_provider_.context(), inspect::Node(), [] {}),
            fxl::WeakPtr<scenic_impl::gfx::SceneGraph>(), /*request_focus*/ [](auto...) {}) {}

  void SetUp() override {
    client1_ptr_.set_error_handler([](auto) { FAIL() << "Client1's channel closed"; });
    client2_ptr_.set_error_handler([](auto) { FAIL() << "Client2's channel closed"; });

    input_system_.OnNewViewTreeSnapshot(NewSnapshot(
        /*hits*/ {}, /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));
    input_system_.RegisterMouseSource(client1_ptr_.NewRequest(), kClient1Koid);
    input_system_.RegisterMouseSource(client2_ptr_.NewRequest(), kClient2Koid);
  }

  // Starts a recursive MouseSource::Watch() loop that collects all received events into
  // |out_events|.
  template <class T, class U>
  void StartWatchLoop(T& mouse_source, U& out_events) {
    const size_t index = watch_loops_.size();
    watch_loops_.emplace_back().emplace<std::function<void(U)>>(
        [this, &mouse_source, &out_events, index](U events) {
          std::move(events.begin(), events.end(), std::back_inserter(out_events));
          mouse_source->Watch([this, index](U events) {
            std::get<std::function<void(U)>>(watch_loops_.at(index))(std::move(events));
          });
        });
    mouse_source->Watch(std::get<std::function<void(U)>>(watch_loops_.at(index)));
  }

 private:
  // Must be initialized before |input_system_|.
  sys::testing::ComponentContextProvider context_provider_;

 protected:
  scenic_impl::input::InputSystem input_system_;
  fuchsia::ui::pointer::MouseSourcePtr client1_ptr_;
  fuchsia::ui::pointer::MouseSourcePtr client2_ptr_;

 private:
  // Holds watch loop state alive for the duration of the test.
  std::vector<std::variant<std::function<void(std::vector<fup_MouseEvent>)>,
                           std::function<void(std::vector<fup_GlobalMouseEvent>)>>>
      watch_loops_;
};

TEST_F(MouseTest, Watch_WithNoInjectedEvents_ShouldNeverReturn) {
  bool callback_triggered = false;
  client1_ptr_->Watch([&callback_triggered](auto) { callback_triggered = true; });

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_triggered);
}

TEST_F(MouseTest, IllegalOperation_ShouldCloseChannel) {
  bool channel_closed = false;
  client1_ptr_.set_error_handler([&channel_closed](auto...) { channel_closed = true; });

  // Illegal operation: calling Watch() twice without getting an event.
  bool callback_triggered = false;
  client1_ptr_->Watch([&callback_triggered](auto) { callback_triggered = true; });
  client1_ptr_->Watch([&callback_triggered](auto) { callback_triggered = true; });
  RunLoopUntilIdle();
  EXPECT_TRUE(channel_closed);
  EXPECT_FALSE(callback_triggered);
}

TEST_F(MouseTest, ExclusiveInjection_ShouldBeDeliveredOnlyToTarget) {
  std::vector<fup_MouseEvent> received_events1;
  StartWatchLoop(client1_ptr_, received_events1);
  std::vector<fup_MouseEvent> received_events2;
  StartWatchLoop(client2_ptr_, received_events2);

  RunLoopUntilIdle();
  EXPECT_TRUE(received_events1.empty());
  EXPECT_TRUE(received_events2.empty());

  input_system_.InjectMouseEventExclusive(MouseEventTemplate(kClient1Koid), kStream1Id);
  RunLoopUntilIdle();
  EXPECT_EQ(received_events1.size(), 1u);
  EXPECT_TRUE(received_events2.empty());

  received_events1.clear();
  input_system_.InjectMouseEventExclusive(MouseEventTemplate(kClient2Koid), kStream2Id);
  RunLoopUntilIdle();
  EXPECT_EQ(received_events2.size(), 1u);
  EXPECT_TRUE(received_events1.empty());
}

TEST_F(MouseTest, HitTestedInjection_WithButtonUp_ShouldBeDeliveredOnlyToTopHit) {
  std::vector<fup_MouseEvent> received_events1;
  StartWatchLoop(client1_ptr_, received_events1);
  std::vector<fup_MouseEvent> received_events2;
  StartWatchLoop(client2_ptr_, received_events2);

  RunLoopUntilIdle();
  EXPECT_TRUE(received_events1.empty());
  EXPECT_TRUE(received_events2.empty());

  // Client 1 is top hit.
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient1Koid, kClient2Koid},
      /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid), kStream1Id);
  RunLoopUntilIdle();
  ASSERT_EQ(received_events1.size(), 1u);
  ASSERT_TRUE(received_events1[0].has_stream_info());
  EXPECT_EQ(received_events1[0].stream_info().status, MouseViewStatus::ENTERED);
  EXPECT_TRUE(received_events2.empty());

  // Client 2 is top hit.
  input_system_.OnNewViewTreeSnapshot(
      NewSnapshot(/*hits*/ {kClient2Koid, kClient1Koid},
                  /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));

  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient2Koid), kStream1Id);
  RunLoopUntilIdle();
  {  // Client 1 gets an exit event, but no pointer sample.
    ASSERT_EQ(received_events1.size(), 2u);
    const auto& event = received_events1[1];
    EXPECT_FALSE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::EXITED);
  }
  {  // Client 2 gets an enter event and a pointer sample.
    ASSERT_EQ(received_events2.size(), 1u);
    const auto& event = received_events2[0];
    EXPECT_TRUE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::ENTERED);
  }
}

TEST_F(MouseTest, HitTestedInjection_WithButtonDown_ShouldLatchToTopHit_AndOnlyDeliverToLatched) {
  std::vector<fup_MouseEvent> received_events1;
  StartWatchLoop(client1_ptr_, received_events1);
  std::vector<fup_MouseEvent> received_events2;
  StartWatchLoop(client2_ptr_, received_events2);

  RunLoopUntilIdle();
  EXPECT_TRUE(received_events1.empty());
  EXPECT_TRUE(received_events2.empty());

  // Client 1 is top hit.
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient1Koid, kClient2Koid},
      /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));

  // Mouse button down.
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid, /*button_down=*/true),
                                          kStream1Id);
  RunLoopUntilIdle();
  EXPECT_EQ(received_events1.size(), 1u);
  EXPECT_TRUE(received_events2.empty());

  // Remove client 1 from the hit test.
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient2Koid}, /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));

  // Button still down. Still delivered to client 1.
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid, /*button_down=*/true),
                                          kStream1Id);
  RunLoopUntilIdle();
  EXPECT_EQ(received_events1.size(), 2u);
  EXPECT_TRUE(received_events2.empty());

  // Button up again. Client 1 gets a "View exited" event and client 2 gets its first hover event.
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid, /*button_down=*/false),
                                          kStream1Id);
  RunLoopUntilIdle();
  {  // Client 1 gets an exit event, but not a pointer sample.
    ASSERT_EQ(received_events1.size(), 3u);
    const auto& event = received_events1[2];
    EXPECT_FALSE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::EXITED);
  }
  {  // Client 2 gets an enter event and a pointer sample.
    ASSERT_EQ(received_events2.size(), 1u);
    const auto& event = received_events2[0];
    EXPECT_TRUE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::ENTERED);
  }
}

TEST_F(MouseTest, LatchedClient_WhenNotInViewTree_ShouldReceiveViewExit) {
  std::vector<fup_MouseEvent> received_events1;
  StartWatchLoop(client1_ptr_, received_events1);
  std::vector<fup_MouseEvent> received_events2;
  StartWatchLoop(client2_ptr_, received_events2);

  RunLoopUntilIdle();
  EXPECT_TRUE(received_events1.empty());
  EXPECT_TRUE(received_events2.empty());

  // Client 2 is top hit.
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient2Koid},
      /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));

  // Mouse button down. Latch on client 2.
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid, /*button_down=*/true),
                                          kStream1Id);
  RunLoopUntilIdle();
  EXPECT_TRUE(received_events1.empty());
  EXPECT_EQ(received_events2.size(), 1u);

  // Remove client 2 from the hit test and the ViewTree
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient1Koid}, /*hierarchy*/ {kContextKoid, kClient1Koid}));

  // Button still down, but client 2 gets a ViewExit event and no more pointer samples.
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid, /*button_down=*/true),
                                          kStream1Id);
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid, /*button_down=*/true),
                                          kStream1Id);
  RunLoopUntilIdle();
  EXPECT_TRUE(received_events1.empty());
  {  // Client 2 gets an exit event but no pointer sample.
    ASSERT_EQ(received_events2.size(), 2u);
    const auto& event = received_events2[1];
    EXPECT_FALSE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::EXITED);
  }

  // Button up. Client 1 gets its first hover event.
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid, /*button_down=*/false),
                                          kStream1Id);
  RunLoopUntilIdle();
  EXPECT_EQ(received_events1.size(), 1u);
  EXPECT_EQ(received_events2.size(), 2u);

  // Client 2 returns.
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient2Koid},
      /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));

  // And correctly gets another hover event.
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid, /*button_down=*/false),
                                          kStream1Id);
  RunLoopUntilIdle();
  EXPECT_EQ(received_events2.size(), 3u);
}

TEST_F(MouseTest, Streams_ShouldLatchIndependently) {
  std::vector<fup_MouseEvent> received_events1;
  StartWatchLoop(client1_ptr_, received_events1);
  std::vector<fup_MouseEvent> received_events2;
  StartWatchLoop(client2_ptr_, received_events2);

  RunLoopUntilIdle();
  EXPECT_TRUE(received_events1.empty());
  EXPECT_TRUE(received_events2.empty());

  // Client 1 is top hit.
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient1Koid, kClient2Koid},
      /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));

  // Mouse button down Stream 1. Should latch to client 1.
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid, /*button_down=*/true),
                                          kStream1Id);
  RunLoopUntilIdle();
  EXPECT_EQ(received_events1.size(), 1u);
  EXPECT_TRUE(received_events2.empty());

  // Client 2 is top hit.
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient2Koid}, /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));

  // Mouse button down Stream 2. Should latch to client 2.
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid, /*button_down=*/true),
                                          kStream2Id);
  RunLoopUntilIdle();
  EXPECT_EQ(received_events1.size(), 1u);
  EXPECT_EQ(received_events2.size(), 1u);

  // Stream 1 should continue going to client 1.
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid, /*button_down=*/true),
                                          kStream1Id);
  RunLoopUntilIdle();
  EXPECT_EQ(received_events1.size(), 2u);
  EXPECT_EQ(received_events2.size(), 1u);

  // Stream 2 should continue going to client 2.
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid, /*button_down=*/true),
                                          kStream2Id);
  RunLoopUntilIdle();
  EXPECT_EQ(received_events1.size(), 2u);
  EXPECT_EQ(received_events2.size(), 2u);
}

TEST_F(MouseTest, EmptyHitTest_ShouldDeliverToNoOne) {
  std::vector<fup_MouseEvent> received_events;
  StartWatchLoop(client1_ptr_, received_events);

  // Client 1 is top hit.
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient1Koid},
      /*hierarchy*/ {kContextKoid, kClient1Koid}));
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid), kStream1Id);
  RunLoopUntilIdle();
  // Client 1 receives events.
  ASSERT_EQ(received_events.size(), 1u);
  ASSERT_TRUE(received_events[0].has_stream_info());
  EXPECT_EQ(received_events[0].stream_info().status, MouseViewStatus::ENTERED);

  // Hit test returns empty.
  input_system_.OnNewViewTreeSnapshot(
      NewSnapshot(/*hits*/ {}, /*hierarchy*/ {kContextKoid, kClient1Koid}));

  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid), kStream1Id);
  RunLoopUntilIdle();
  {  // Client 1 gets an exit event, but no pointer sample.
    ASSERT_EQ(received_events.size(), 2u);
    const auto& event = received_events[1];
    EXPECT_FALSE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::EXITED);
  }
  received_events.clear();

  // Next injections returns nothing.
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid), kStream1Id);
  RunLoopUntilIdle();
  EXPECT_TRUE(received_events.empty());

  // Button down returns nothing.
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid, /*button_down=*/true),
                                          kStream1Id);
  RunLoopUntilIdle();
  EXPECT_TRUE(received_events.empty());

  // Client 1 is top hit.
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient1Koid},
      /*hierarchy*/ {kContextKoid, kClient1Koid}));

  // Button up. Client 1 should now receive a hover event.
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid, /*button_down=*/false),
                                          kStream1Id);
  RunLoopUntilIdle();
  EXPECT_EQ(received_events.size(), 1u);
}

TEST_F(MouseTest, CancelMouseStream_ShouldSendEvent_OnlyWhenThereIsOngoingStream) {
  std::vector<fup_MouseEvent> received_events1;
  StartWatchLoop(client1_ptr_, received_events1);
  std::vector<fup_MouseEvent> received_events2;
  StartWatchLoop(client2_ptr_, received_events2);

  RunLoopUntilIdle();
  EXPECT_TRUE(received_events1.empty());
  EXPECT_TRUE(received_events2.empty());

  // Client 1 is top hit.
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient1Koid, kClient2Koid},
      /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));

  // Mouse button down Stream 1. Should latch to client 1.
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid, /*button_down=*/true),
                                          kStream1Id);
  RunLoopUntilIdle();
  EXPECT_EQ(received_events1.size(), 1u);
  EXPECT_TRUE(received_events2.empty());

  // Client 2 is top hit.
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient2Koid}, /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));

  // Hover on stream 2. Should send to client 2
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid, /*button_down=*/true),
                                          kStream2Id);
  RunLoopUntilIdle();
  EXPECT_EQ(received_events1.size(), 1u);
  EXPECT_EQ(received_events2.size(), 1u);

  // Cancelling stream 1 should deliver view exited event to client 1.
  input_system_.CancelMouseStream(kStream1Id);
  RunLoopUntilIdle();
  {
    ASSERT_EQ(received_events1.size(), 2u);
    const auto& event = received_events1[1];
    EXPECT_FALSE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::EXITED);
  }
  EXPECT_EQ(received_events2.size(), 1u);

  // Cancelling stream 2 should deliver view exited event to client 2.
  input_system_.CancelMouseStream(kStream2Id);
  RunLoopUntilIdle();
  EXPECT_EQ(received_events1.size(), 2u);
  {
    ASSERT_EQ(received_events2.size(), 2u);
    const auto& event = received_events2[1];
    EXPECT_FALSE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::EXITED);
  }

  received_events1.clear();
  received_events2.clear();

  // More cancel events should be no-ops.
  input_system_.CancelMouseStream(kStream1Id);
  input_system_.CancelMouseStream(kStream2Id);
  RunLoopUntilIdle();
  EXPECT_TRUE(received_events1.empty());
  EXPECT_TRUE(received_events2.empty());

  // Hover on stream 2. Should send to client 2
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid, /*button_down=*/false),
                                          kStream2Id);
  // No top hit.
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {}, /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));
  // Client 2 gets a view exited event on the next one.
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid, /*button_down=*/false),
                                          kStream2Id);
  RunLoopUntilIdle();
  EXPECT_EQ(received_events2.size(), 2u);
  received_events2.clear();

  // Cancelling stream now should be a no-op.
  input_system_.CancelMouseStream(kStream2Id);
  RunLoopUntilIdle();
  EXPECT_TRUE(received_events2.empty());
}

// This case should also cover when the target is below the MouseSourceWithGlobalMouse in the view
// tree, since hits from below are impossible.
TEST_F(MouseTest, MouseSourceWithGlobalMouse_DoesNotGetEventsWhenNotHit) {
  // Set up a MouseSourceWithGlobalMouse for client 1.
  fuchsia::ui::pointer::augment::MouseSourceWithGlobalMousePtr global_client_ptr;
  input_system_.Upgrade(std::move(client1_ptr_), [&global_client_ptr](auto new_handle, auto _) {
    global_client_ptr.Bind(std::move(new_handle));
  });
  RunLoopUntilIdle();

  std::vector<fup_GlobalMouseEvent> received_events;
  StartWatchLoop(global_client_ptr, received_events);

  // Inject with client 1 as the target, but nothing is hit.
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {},
      /*hierarchy*/ {kContextKoid, kClient1Koid}));
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid), kStream1Id);
  RunLoopUntilIdle();

  EXPECT_TRUE(received_events.empty()) << "Should get no events when not hit.";
}

TEST_F(MouseTest, MouseSourceWithGlobalMouse_GetsEventsOriginatingFromAbove) {
  // Set up a MouseSourceWithGlobalMouse for client 2.
  fuchsia::ui::pointer::augment::MouseSourceWithGlobalMousePtr global_client_ptr;
  input_system_.Upgrade(std::move(client2_ptr_), [&global_client_ptr](auto new_handle, auto _) {
    global_client_ptr.Bind(std::move(new_handle));
  });
  RunLoopUntilIdle();

  std::vector<fup_GlobalMouseEvent> received_events;
  StartWatchLoop(global_client_ptr, received_events);

  // Client 1 is above client 2 in the view hierarchy, and client 2 is hit.
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient2Koid},
      /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));
  // Inject with client 1 as the target.
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid), kStream1Id);
  RunLoopUntilIdle();

  // Client 2 should only get both local and global event.
  ASSERT_EQ(received_events.size(), 1u);
  const auto& event = received_events.front();
  EXPECT_TRUE(event.has_mouse_event());
  EXPECT_TRUE(event.has_global_position());
  ASSERT_TRUE(event.has_global_stream_info());
  EXPECT_EQ(event.global_stream_info().status, MouseViewStatus::ENTERED);
}

TEST_F(MouseTest, MouseSourceWithGlobalMouse_GetsEventsWithSelfAsTarget) {
  // Set up a MouseSourceWithGlobalMouse for client 1.
  fuchsia::ui::pointer::augment::MouseSourceWithGlobalMousePtr global_client_ptr;
  input_system_.Upgrade(std::move(client1_ptr_), [&global_client_ptr](auto new_handle, auto _) {
    global_client_ptr.Bind(std::move(new_handle));
  });
  RunLoopUntilIdle();

  std::vector<fup_GlobalMouseEvent> received_events;
  StartWatchLoop(global_client_ptr, received_events);

  // Inject with client 1 as the target, and client 2 is top hit.
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient2Koid, kClient1Koid},
      /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid), kStream1Id);
  RunLoopUntilIdle();

  // Client 1 should only get global events.
  ASSERT_EQ(received_events.size(), 1u);
  const auto& event = received_events.front();
  EXPECT_FALSE(event.has_mouse_event());
  EXPECT_TRUE(event.has_global_position());
  ASSERT_TRUE(event.has_global_stream_info());
  EXPECT_EQ(event.global_stream_info().status, MouseViewStatus::ENTERED);
}

TEST_F(MouseTest, MouseSourceWithGlobalMouse_GetsEventsForExclusiveInjection) {
  // Set up a MouseSourceWithGlobalMouse for client 1.
  fuchsia::ui::pointer::augment::MouseSourceWithGlobalMousePtr global_client_ptr;
  input_system_.Upgrade(std::move(client1_ptr_), [&global_client_ptr](auto new_handle, auto _) {
    global_client_ptr.Bind(std::move(new_handle));
  });
  RunLoopUntilIdle();

  std::vector<fup_GlobalMouseEvent> received_events;
  StartWatchLoop(global_client_ptr, received_events);

  // Nothing is hit.
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {},
      /*hierarchy*/ {kContextKoid, kClient1Koid}));
  // Inject with client 1 as the target.
  input_system_.InjectMouseEventExclusive(MouseEventTemplate(kClient1Koid), kStream1Id);
  RunLoopUntilIdle();

  {  // Client should get only normal event, since the injection was outside the view.
    ASSERT_EQ(received_events.size(), 1u);
    const auto& event = received_events.front();
    EXPECT_TRUE(event.has_mouse_event());
    EXPECT_FALSE(event.has_global_position());
    EXPECT_FALSE(event.has_global_stream_info());
  }
  received_events.clear();

  // Client 1 is hit.
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient1Koid},
      /*hierarchy*/ {kContextKoid, kClient1Koid}));
  // Inject with client 1 as the target.
  input_system_.InjectMouseEventExclusive(MouseEventTemplate(kClient1Koid), kStream1Id);
  RunLoopUntilIdle();

  {  // Client should get both normal and global events, since we're now hovering over the view.
    ASSERT_EQ(received_events.size(), 1u);
    const auto& event = received_events.front();
    EXPECT_TRUE(event.has_mouse_event());
    EXPECT_TRUE(event.has_global_position());
    ASSERT_TRUE(event.has_global_stream_info());
    EXPECT_EQ(event.global_stream_info().status, MouseViewStatus::ENTERED);
  }
}

TEST_F(MouseTest, MouseSourceWithGlobalMouseTest) {
  fuchsia::ui::pointer::augment::MouseSourceWithGlobalMousePtr global_client1_ptr;
  input_system_.Upgrade(std::move(client1_ptr_), [&global_client1_ptr](auto new_handle, auto _) {
    global_client1_ptr.Bind(std::move(new_handle));
  });
  fuchsia::ui::pointer::augment::MouseSourceWithGlobalMousePtr global_client2_ptr;
  input_system_.Upgrade(std::move(client2_ptr_), [&global_client2_ptr](auto new_handle, auto _) {
    global_client2_ptr.Bind(std::move(new_handle));
  });
  RunLoopUntilIdle();

  std::vector<fup_GlobalMouseEvent> received_events1;
  StartWatchLoop(global_client1_ptr, received_events1);
  std::vector<fup_GlobalMouseEvent> received_events2;
  StartWatchLoop(global_client2_ptr, received_events2);

  RunLoopUntilIdle();
  EXPECT_TRUE(received_events1.empty());
  EXPECT_TRUE(received_events2.empty());

  // Client 1 is top and only hit
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient1Koid},
      /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));
  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid), kStream1Id);
  RunLoopUntilIdle();

  {  // Client 1 should get global data and normal data.
    ASSERT_EQ(received_events1.size(), 1u);
    const auto& event = received_events1.front();
    EXPECT_TRUE(event.has_mouse_event());
    EXPECT_TRUE(event.has_global_position());
    ASSERT_TRUE(event.has_global_stream_info());
    EXPECT_EQ(event.global_stream_info().status, MouseViewStatus::ENTERED);
  }
  // Client 2 should get nothing.
  EXPECT_TRUE(received_events2.empty());
  received_events1.clear();

  // Client 2 is top hit, but client 1 is still in the hit list.
  input_system_.OnNewViewTreeSnapshot(
      NewSnapshot(/*hits*/ {kClient2Koid, kClient1Koid},
                  /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));

  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid), kStream1Id);
  RunLoopUntilIdle();
  {  // Client 1 gets global data and a view exited event on the normal path.
    ASSERT_EQ(received_events1.size(), 1u);
    const auto& event = received_events1.front();
    ASSERT_TRUE(event.has_mouse_event());
    ASSERT_TRUE(event.mouse_event().has_stream_info());
    EXPECT_EQ(event.mouse_event().stream_info().status, MouseViewStatus::EXITED);
    EXPECT_TRUE(event.has_global_position());
    EXPECT_FALSE(event.has_global_stream_info());
  }
  {  // Client 2 gets an enter event and the normal data.
    ASSERT_EQ(received_events2.size(), 1u);
    const auto& event = received_events2.front();
    EXPECT_TRUE(event.has_mouse_event());
    EXPECT_TRUE(event.has_global_position());
    ASSERT_TRUE(event.has_global_stream_info());
    EXPECT_EQ(event.global_stream_info().status, MouseViewStatus::ENTERED);
  }
  received_events1.clear();
  received_events2.clear();

  // No hits.
  input_system_.OnNewViewTreeSnapshot(
      NewSnapshot(/*hits*/ {},
                  /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));

  input_system_.InjectMouseEventHitTested(MouseEventTemplate(kClient1Koid), kStream1Id);
  RunLoopUntilIdle();
  {  // Client 1 gets only global data and a global view exited event.
    ASSERT_EQ(received_events1.size(), 1u);
    const auto& event = received_events1.front();
    EXPECT_FALSE(event.has_mouse_event());
    EXPECT_TRUE(event.has_global_position());
    ASSERT_TRUE(event.has_global_stream_info());
    EXPECT_EQ(event.global_stream_info().status, MouseViewStatus::EXITED);
  }
  {  // Client 2 gets global data, a global view exited event AND a view exited event on the normal
     // path.
    ASSERT_EQ(received_events2.size(), 1u);
    const auto& event = received_events2.front();
    EXPECT_TRUE(event.has_mouse_event());
    ASSERT_TRUE(event.mouse_event().has_stream_info());
    EXPECT_EQ(event.mouse_event().stream_info().status, MouseViewStatus::EXITED);
    EXPECT_TRUE(event.has_global_position());
    ASSERT_TRUE(event.has_global_stream_info());
    EXPECT_EQ(event.global_stream_info().status, MouseViewStatus::EXITED);
  }
}

}  // namespace input::test
