// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testing/test_loop.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/scenic/lib/input/touch_source.h"
#include "src/ui/scenic/lib/input/touch_system.h"

// These tests exercise the full gesture disambiguation implementation of InputSystem for
// clients of the  fuchsia.ui.pointer.TouchSource protocol.

namespace input::test {

using fup_EventPhase = fuchsia::ui::pointer::EventPhase;
using fup_TouchEvent = fuchsia::ui::pointer::TouchEvent;
using fup_TouchResponse = fuchsia::ui::pointer::TouchResponse;
using fup_TouchResponseType = fuchsia::ui::pointer::TouchResponseType;
using fup_TouchInteractionStatus = fuchsia::ui::pointer::TouchInteractionStatus;

using scenic_impl::input::InternalTouchEvent;
using scenic_impl::input::Phase;
using scenic_impl::input::StreamId;
using scenic_impl::input::TouchSource;

constexpr zx_koid_t kContextKoid = 100u;
constexpr zx_koid_t kClient1Koid = 1u;
constexpr zx_koid_t kClient2Koid = 2u;

constexpr StreamId kStream1Id = 11u;
constexpr StreamId kStream2Id = 22u;

namespace {

InternalTouchEvent PointerEventTemplate(zx_koid_t target) {
  InternalTouchEvent event{
      .timestamp = 0,
      .device_id = 1u,
      .pointer_id = 1u,
      .phase = Phase::kAdd,
      .context = kContextKoid,
      .target = target,
      .position_in_viewport = glm::vec2(5, 5),
      .buttons = 0,
  };

  event.viewport.extents.min = {0, 0};
  event.viewport.extents.max = {10, 10};

  return event;
}

fup_TouchResponse MakeTouchResponse(fup_TouchResponseType response_type) {
  fup_TouchResponse response;
  response.set_response_type(response_type);
  return response;
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

  snapshot->hit_testers.emplace_back([hits = std::move(hits)](auto...) mutable {
    return view_tree::SubtreeHitTestResult{.hits = std::move(hits)};
  });

  return snapshot;
}

}  // namespace

class GestureDisambiguationTest : public gtest::TestLoopFixture {
 public:
  GestureDisambiguationTest()
      : hit_tester_(view_tree_snapshot_, inspect_node_),
        touch_system_(context_provider_.context(), view_tree_snapshot_, hit_tester_, inspect_node_,
                      nullptr) {}

  void SetUp() override {
    client1_ptr_.set_error_handler([](auto) { FAIL() << "Client1's channel closed"; });
    client2_ptr_.set_error_handler([](auto) { FAIL() << "Client2's channel closed"; });

    OnNewViewTreeSnapshot(NewSnapshot(
        /*hits*/ {}, /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));
    touch_system_.RegisterTouchSource(client1_ptr_.NewRequest(), kClient1Koid);
    touch_system_.RegisterTouchSource(client2_ptr_.NewRequest(), kClient2Koid);
  }

  void OnNewViewTreeSnapshot(std::shared_ptr<const view_tree::Snapshot> snapshot) {
    view_tree_snapshot_ = snapshot;
  }

 private:
  // Must be initialized before |touch_system_|.
  sys::testing::ComponentContextProvider context_provider_;

 protected:
  inspect::Node inspect_node_;
  std::shared_ptr<const view_tree::Snapshot> view_tree_snapshot_;
  scenic_impl::input::HitTester hit_tester_;
  scenic_impl::input::TouchSystem touch_system_;
  fuchsia::ui::pointer::TouchSourcePtr client1_ptr_;
  fuchsia::ui::pointer::TouchSourcePtr client2_ptr_;
};

TEST_F(GestureDisambiguationTest, Watch_WithNoInjectedEvents_ShouldNeverReturn) {
  bool callback_triggered = false;
  client1_ptr_->Watch({}, [&callback_triggered](auto) { callback_triggered = true; });

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_triggered);
}

TEST_F(GestureDisambiguationTest, IllegalOperation_ShouldCloseChannel) {
  bool channel_closed = false;
  client1_ptr_.set_error_handler([&channel_closed](auto...) { channel_closed = true; });

  // Illegal operation: calling Watch() twice without getting an event.
  bool callback_triggered = false;
  client1_ptr_->Watch({}, [&callback_triggered](auto) { callback_triggered = true; });
  client1_ptr_->Watch({}, [&callback_triggered](auto) { callback_triggered = true; });
  RunLoopUntilIdle();
  EXPECT_TRUE(channel_closed);
  EXPECT_FALSE(callback_triggered);
}

TEST_F(GestureDisambiguationTest, ExclusiveInjection_ShouldBeDeliveredOnlyToTarget_AndBeGranted) {
  std::vector<fup_TouchEvent> received_events1;
  client1_ptr_->Watch({}, [&received_events1](auto events) {
    std::move(events.begin(), events.end(), std::back_inserter(received_events1));
  });
  std::vector<fup_TouchEvent> received_events2;
  client2_ptr_->Watch({}, [&received_events2](auto events) {
    std::move(events.begin(), events.end(), std::back_inserter(received_events2));
  });

  RunLoopUntilIdle();
  EXPECT_TRUE(received_events1.empty());
  EXPECT_TRUE(received_events2.empty());

  touch_system_.InjectTouchEventExclusive(PointerEventTemplate(kClient1Koid), kStream1Id);
  RunLoopUntilIdle();
  EXPECT_EQ(received_events1.size(), 1u);
  ASSERT_TRUE(received_events1[0].has_interaction_result());
  EXPECT_EQ(received_events1[0].interaction_result().status, fup_TouchInteractionStatus::GRANTED);
  EXPECT_TRUE(received_events2.empty());

  received_events1.clear();
  touch_system_.InjectTouchEventExclusive(PointerEventTemplate(kClient2Koid), kStream2Id);
  RunLoopUntilIdle();
  ASSERT_EQ(received_events2.size(), 1u);
  ASSERT_TRUE(received_events2[0].has_interaction_result());
  EXPECT_EQ(received_events2[0].interaction_result().status, fup_TouchInteractionStatus::GRANTED);
  EXPECT_TRUE(received_events1.empty());
}

TEST_F(GestureDisambiguationTest,
       InjectionThatHitsClientWithoutValidAncestors_ShouldBeDeliveredAndBeGranted) {
  std::vector<fup_TouchEvent> received_events1;
  client1_ptr_->Watch({}, [&received_events1](auto events) {
    std::move(events.begin(), events.end(), std::back_inserter(received_events1));
  });
  std::vector<fup_TouchEvent> received_events2;
  client2_ptr_->Watch({}, [&received_events2](auto events) {
    std::move(events.begin(), events.end(), std::back_inserter(received_events2));
  });

  RunLoopUntilIdle();
  EXPECT_TRUE(received_events1.empty());
  EXPECT_TRUE(received_events2.empty());

  OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient1Koid}, /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));

  touch_system_.InjectTouchEventHitTested(PointerEventTemplate(kClient1Koid), kStream1Id);
  RunLoopUntilIdle();
  ASSERT_EQ(received_events1.size(), 1u);
  ASSERT_TRUE(received_events1[0].has_interaction_result());
  EXPECT_EQ(received_events1[0].interaction_result().status, fup_TouchInteractionStatus::GRANTED);
  EXPECT_TRUE(received_events2.empty());

  OnNewViewTreeSnapshot(
      NewSnapshot(/*hits*/ {kClient2Koid}, /*hierarchy*/ {kContextKoid, kClient2Koid}));

  touch_system_.InjectTouchEventHitTested(PointerEventTemplate(kClient2Koid), kStream2Id);
  RunLoopUntilIdle();
  ASSERT_EQ(received_events2.size(), 1u);
  ASSERT_TRUE(received_events2[0].has_interaction_result());
  EXPECT_EQ(received_events2[0].interaction_result().status, fup_TouchInteractionStatus::GRANTED);
}

TEST_F(GestureDisambiguationTest,
       InjectionThatHitsClientWithValidAncestor_ShouldBeDeliveredToBoth) {
  std::vector<fup_TouchEvent> received_events1;
  client1_ptr_->Watch({}, [&received_events1](auto events) {
    std::move(events.begin(), events.end(), std::back_inserter(received_events1));
  });
  std::vector<fup_TouchEvent> received_events2;
  client2_ptr_->Watch({}, [&received_events2](auto events) {
    std::move(events.begin(), events.end(), std::back_inserter(received_events2));
  });

  RunLoopUntilIdle();
  EXPECT_TRUE(received_events1.empty());
  EXPECT_TRUE(received_events2.empty());

  OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient2Koid}, /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));
  touch_system_.InjectTouchEventHitTested(PointerEventTemplate(kClient1Koid), kStream1Id);

  RunLoopUntilIdle();
  ASSERT_EQ(received_events1.size(), 1u);
  EXPECT_FALSE(received_events1.front().has_interaction_result());
  ASSERT_EQ(received_events2.size(), 1u);
  EXPECT_FALSE(received_events2.front().has_interaction_result());

  {
    std::vector<fup_TouchResponse> responses;
    responses.emplace_back(MakeTouchResponse(fup_TouchResponseType::MAYBE));
    client1_ptr_->Watch(std::move(responses), [&received_events1](auto events) {
      std::move(events.begin(), events.end(), std::back_inserter(received_events1));
    });
  }
  {
    std::vector<fup_TouchResponse> responses;
    responses.emplace_back(MakeTouchResponse(fup_TouchResponseType::MAYBE));
    client2_ptr_->Watch(std::move(responses), [&received_events2](auto events) {
      std::move(events.begin(), events.end(), std::back_inserter(received_events2));
    });
  }

  // No one should be granted the win yet, so expect no more events.
  RunLoopUntilIdle();
  EXPECT_EQ(received_events1.size(), 1u);
  EXPECT_EQ(received_events2.size(), 1u);
}

TEST_F(GestureDisambiguationTest, Contest_ShouldNotIncludeContext) {
  std::vector<fup_TouchEvent> received_events1;
  client1_ptr_->Watch({}, [&received_events1](auto events) {
    std::move(events.begin(), events.end(), std::back_inserter(received_events1));
  });
  std::vector<fup_TouchEvent> received_events2;
  client2_ptr_->Watch({}, [&received_events2](auto events) {
    std::move(events.begin(), events.end(), std::back_inserter(received_events2));
  });

  RunLoopUntilIdle();
  EXPECT_TRUE(received_events1.empty());
  EXPECT_TRUE(received_events2.empty());

  // Inject an event with kClient1Koid as the context and kClient2Koid as the target.
  OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient2Koid}, /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));
  auto event = PointerEventTemplate(kClient2Koid);
  event.context = kClient1Koid;
  touch_system_.InjectTouchEventHitTested(event, kStream1Id);

  RunLoopUntilIdle();
  EXPECT_TRUE(received_events1.empty()) << "The context should not receive any events.";
  EXPECT_EQ(received_events2.size(), 1u);
}

TEST_F(GestureDisambiguationTest, EveryoneRespondsYesPrioritize_ShouldResolveToHighestPriority) {
  std::vector<fup_TouchEvent> received_events1;
  client1_ptr_->Watch({}, [&received_events1](auto events) {
    std::move(events.begin(), events.end(), std::back_inserter(received_events1));
  });
  std::vector<fup_TouchEvent> received_events2;
  client2_ptr_->Watch({}, [&received_events2](auto events) {
    std::move(events.begin(), events.end(), std::back_inserter(received_events2));
  });

  RunLoopUntilIdle();
  EXPECT_TRUE(received_events1.empty());
  EXPECT_TRUE(received_events2.empty());

  OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient2Koid}, /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));
  touch_system_.InjectTouchEventHitTested(PointerEventTemplate(kClient1Koid), kStream1Id);

  RunLoopUntilIdle();
  ASSERT_EQ(received_events1.size(), 1u);
  EXPECT_FALSE(received_events1.front().has_interaction_result());
  ASSERT_EQ(received_events2.size(), 1u);
  EXPECT_FALSE(received_events2.front().has_interaction_result());

  // Both try to claim the stream, but client1 has higher priority and should win.
  {
    std::vector<fup_TouchResponse> responses;
    responses.emplace_back(MakeTouchResponse(fup_TouchResponseType::YES_PRIORITIZE));
    client1_ptr_->Watch(std::move(responses), [&received_events1](auto events) {
      std::move(events.begin(), events.end(), std::back_inserter(received_events1));
    });
  }
  {
    std::vector<fup_TouchResponse> responses;
    responses.emplace_back(MakeTouchResponse(fup_TouchResponseType::YES_PRIORITIZE));
    client2_ptr_->Watch(std::move(responses), [&received_events2](auto events) {
      std::move(events.begin(), events.end(), std::back_inserter(received_events2));
    });
  }

  // Both should have received an event with a TouchInteractionStatus.
  RunLoopUntilIdle();
  ASSERT_EQ(received_events1.size(), 2u);
  ASSERT_TRUE(received_events1[1].has_interaction_result());
  EXPECT_EQ(received_events1[1].interaction_result().status, fup_TouchInteractionStatus::GRANTED);
  ASSERT_EQ(received_events2.size(), 2u);
  ASSERT_TRUE(received_events2[1].has_interaction_result());
  EXPECT_EQ(received_events2[1].interaction_result().status, fup_TouchInteractionStatus::DENIED);

  // Subsequent events should only go to the winner.
  {
    std::vector<fup_TouchResponse> responses;
    responses.emplace_back();
    client1_ptr_->Watch(std::move(responses), [&received_events1](auto events) {
      std::move(events.begin(), events.end(), std::back_inserter(received_events1));
    });
  }
  {
    std::vector<fup_TouchResponse> responses;
    responses.emplace_back();
    client2_ptr_->Watch(std::move(responses), [&received_events2](auto events) {
      std::move(events.begin(), events.end(), std::back_inserter(received_events2));
    });
  }

  auto event = PointerEventTemplate(kClient1Koid);
  event.phase = Phase::kChange;
  touch_system_.InjectTouchEventHitTested(event, kStream1Id);

  RunLoopUntilIdle();
  EXPECT_EQ(received_events1.size(), 3u);
  ASSERT_EQ(received_events2.size(), 2u);
}

TEST_F(GestureDisambiguationTest, EveryonRespondsMaybe_ShouldResolveAtStreamEnd) {
  OnNewViewTreeSnapshot(NewSnapshot(
      /*hits*/ {kClient2Koid}, /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));

  // Inject one event for each phase:
  {
    auto event = PointerEventTemplate(kClient1Koid);
    event.phase = Phase::kAdd;
    touch_system_.InjectTouchEventHitTested(event, kStream1Id);
    event.phase = Phase::kChange;
    touch_system_.InjectTouchEventHitTested(event, kStream1Id);
    event.phase = Phase::kRemove;
    touch_system_.InjectTouchEventHitTested(event, kStream1Id);
  }

  std::vector<fup_TouchEvent> received_events1;
  client1_ptr_->Watch({}, [&received_events1](auto events) {
    std::move(events.begin(), events.end(), std::back_inserter(received_events1));
  });
  std::vector<fup_TouchEvent> received_events2;
  client2_ptr_->Watch({}, [&received_events2](auto events) {
    std::move(events.begin(), events.end(), std::back_inserter(received_events2));
  });

  RunLoopUntilIdle();
  EXPECT_EQ(received_events1.size(), 3u);
  EXPECT_EQ(received_events2.size(), 3u);

  // Both respond MAYBE for the entire stream. Client2 has lower priority and should win at stream
  // end.
  {
    std::vector<fup_TouchResponse> responses(received_events1.size());
    std::generate(responses.begin(), responses.end(),
                  [] { return MakeTouchResponse(fup_TouchResponseType::MAYBE); });
    client1_ptr_->Watch(std::move(responses), [&received_events1](auto events) {
      std::move(events.begin(), events.end(), std::back_inserter(received_events1));
    });
  }
  {
    std::vector<fup_TouchResponse> responses(received_events2.size());
    std::generate(responses.begin(), responses.end(),
                  [] { return MakeTouchResponse(fup_TouchResponseType::MAYBE); });
    client2_ptr_->Watch(std::move(responses), [&received_events2](auto events) {
      std::move(events.begin(), events.end(), std::back_inserter(received_events2));
    });
  }

  // Both should have received an event with a TouchInteractionStatus.
  RunLoopUntilIdle();
  ASSERT_TRUE(received_events1.back().has_interaction_result());
  EXPECT_EQ(received_events1.back().interaction_result().status,
            fup_TouchInteractionStatus::DENIED);
  ASSERT_TRUE(received_events2.back().has_interaction_result());
  EXPECT_EQ(received_events2.back().interaction_result().status,
            fup_TouchInteractionStatus::GRANTED);
}

TEST_F(GestureDisambiguationTest, MidStreamChannelClose_ShouldGrantStreamToCompetitor) {
  OnNewViewTreeSnapshot(NewSnapshot(/*hits*/ {kClient2Koid},
                                    /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));
  touch_system_.InjectTouchEventHitTested(PointerEventTemplate(kClient1Koid), kStream1Id);

  {
    std::vector<fup_TouchEvent> received_events;
    client1_ptr_->Watch({}, [&received_events](auto events) {
      std::move(events.begin(), events.end(), std::back_inserter(received_events));
    });

    RunLoopUntilIdle();
    EXPECT_EQ(received_events.size(), 1u);
  }
  {
    std::vector<fup_TouchEvent> received_events;
    client2_ptr_->Watch({}, [&received_events](auto events) {
      std::move(events.begin(), events.end(), std::back_inserter(received_events));
    });
    RunLoopUntilIdle();
    EXPECT_EQ(received_events.size(), 1u);
  }

  // Close client1's channel.
  client1_ptr_.Unbind();

  {  // Observe client2 winning the contest.
    std::vector<fup_TouchEvent> received_events;
    std::vector<fup_TouchResponse> responses;
    responses.emplace_back(MakeTouchResponse(fup_TouchResponseType::MAYBE));
    client2_ptr_->Watch(std::move(responses), [&received_events](auto events) {
      std::move(events.begin(), events.end(), std::back_inserter(received_events));
    });

    RunLoopUntilIdle();
    ASSERT_EQ(received_events.size(), 1u);
    ASSERT_TRUE(received_events.front().has_interaction_result());
    EXPECT_EQ(received_events.front().interaction_result().status,
              fup_TouchInteractionStatus::GRANTED);
  }
}

TEST_F(GestureDisambiguationTest, MidStreamChannelForcedClose_ShouldGrantStreamToCompetitor) {
  bool channel_closed = false;
  client1_ptr_.set_error_handler([&channel_closed](auto...) { channel_closed = true; });

  OnNewViewTreeSnapshot(NewSnapshot(/*hits*/ {kClient2Koid},
                                    /*hierarchy*/ {kContextKoid, kClient1Koid, kClient2Koid}));
  touch_system_.InjectTouchEventHitTested(PointerEventTemplate(kClient1Koid), kStream1Id);

  {
    std::vector<fup_TouchEvent> received_events;
    client1_ptr_->Watch({}, [&received_events](auto events) {
      std::move(events.begin(), events.end(), std::back_inserter(received_events));
    });

    RunLoopUntilIdle();
    EXPECT_EQ(received_events.size(), 1u);
  }
  {
    std::vector<fup_TouchEvent> received_events;
    client2_ptr_->Watch({}, [&received_events](auto events) {
      std::move(events.begin(), events.end(), std::back_inserter(received_events));
    });
    RunLoopUntilIdle();
    EXPECT_EQ(received_events.size(), 1u);
  }

  {  // Illegal operation: empty response vector after first call. Observe channel close.
    EXPECT_FALSE(channel_closed);
    bool callback_triggered = false;
    client1_ptr_->Watch({}, [&callback_triggered](auto...) { callback_triggered = false; });
    RunLoopUntilIdle();
    EXPECT_TRUE(channel_closed);
    EXPECT_FALSE(callback_triggered);
  }

  {  // Observe client2 winning the contest.
    std::vector<fup_TouchEvent> received_events;
    std::vector<fup_TouchResponse> responses;
    responses.emplace_back(MakeTouchResponse(fup_TouchResponseType::MAYBE));
    client2_ptr_->Watch(std::move(responses), [&received_events](auto events) {
      std::move(events.begin(), events.end(), std::back_inserter(received_events));
    });
    RunLoopUntilIdle();
    ASSERT_EQ(received_events.size(), 1u);
    ASSERT_TRUE(received_events.front().has_interaction_result());
    EXPECT_EQ(received_events.front().interaction_result().status,
              fup_TouchInteractionStatus::GRANTED);
  }
}

}  // namespace input::test
