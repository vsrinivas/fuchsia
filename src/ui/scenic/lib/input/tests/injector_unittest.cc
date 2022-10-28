// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fpromise/single_threaded_executor.h>
#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lib/inspect/cpp/hierarchy.h"
#include "lib/inspect/cpp/inspector.h"
#include "lib/inspect/cpp/reader.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/scenic/lib/input/touch_injector.h"
#include "src/ui/scenic/lib/utils/math.h"

using Phase = fuchsia::ui::pointerinjector::EventPhase;
using fuchsia::ui::pointerinjector::DevicePtr;
using fuchsia::ui::pointerinjector::DeviceType;
using InjectionEvent = fuchsia::ui::pointerinjector::Event;
using StreamId = scenic_impl::input::StreamId;

// Unit tests for the Injector class.

namespace input::test {

namespace {

// clang-format off
static constexpr std::array<float, 9> kIdentityMatrix = {
  1, 0, 0, // first column
  0, 1, 0, // second column
  0, 0, 1, // third column
};
// clang-format on

scenic_impl::input::InjectorSettings InjectorSettingsTemplate() {
  return {.dispatch_policy = fuchsia::ui::pointerinjector::DispatchPolicy::EXCLUSIVE_TARGET,
          .device_id = 1,
          .device_type = DeviceType::TOUCH,
          .context_koid = 1,
          .target_koid = 2};
}

scenic_impl::input::Viewport ViewportTemplate() {
  return {
      .extents = std::array<std::array<float, 2>, 2>{{{0, 0}, {1000, 1000}}},
      .context_from_viewport_transform = utils::ColumnMajorMat3ArrayToMat4(kIdentityMatrix),
  };
}

InjectionEvent InjectionEventTemplate() {
  InjectionEvent event;
  event.set_timestamp(1111);
  {
    fuchsia::ui::pointerinjector::PointerSample pointer_sample;
    pointer_sample.set_pointer_id(2222);
    pointer_sample.set_phase(Phase::CHANGE);
    pointer_sample.set_position_in_viewport({333, 444});
    fuchsia::ui::pointerinjector::Data data;
    data.set_pointer_sample(std::move(pointer_sample));
    event.set_data(std::move(data));
  }
  return event;
}

}  // namespace

TEST(InjectorTest, InjectedEvents_ShouldTriggerTheInjectLambda) {
  async::TestLoop test_loop;

  // Set up an isolated Injector.
  DevicePtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  bool connectivity_is_good = true;
  uint32_t num_injections = 0;
  scenic_impl::input::TouchInjector injector_impl(
      inspect::Node(), InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [&connectivity_is_good](zx_koid_t, zx_koid_t) { return connectivity_is_good; },
      /*inject=*/[&num_injections](auto...) { ++num_injections; },
      /*on_channel_closed=*/[] {});

  {  // Inject one event.
    bool injection_callback_fired = false;
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_phase(Phase::ADD);
    std::vector<InjectionEvent> events;
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)},
                     [&injection_callback_fired] { injection_callback_fired = true; });
    test_loop.RunUntilIdle();
    EXPECT_TRUE(injection_callback_fired);
  }

  EXPECT_EQ(num_injections, 1u);

  {  // Inject CHANGE event.
    bool injection_callback_fired = false;
    std::vector<InjectionEvent> events;
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_phase(Phase::CHANGE);
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)},
                     [&injection_callback_fired] { injection_callback_fired = true; });
    test_loop.RunUntilIdle();
    EXPECT_TRUE(injection_callback_fired);

    EXPECT_EQ(num_injections, 2u);
  }

  {  // Inject remove event.
    bool injection_callback_fired = false;
    std::vector<InjectionEvent> events;
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_phase(Phase::REMOVE);
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)},
                     [&injection_callback_fired] { injection_callback_fired = true; });
    test_loop.RunUntilIdle();
    EXPECT_TRUE(injection_callback_fired);
  }

  EXPECT_EQ(num_injections, 3u);
  EXPECT_FALSE(error_callback_fired);
}

TEST(InjectorTest, InjectionWithNoEvent_ShouldCloseChannel) {
  // Test loop to be able to control dispatch without having to create an entire test class
  // subclassing TestLoopFixture.
  async::TestLoop test_loop;

  // Set up an isolated Injector.
  DevicePtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  scenic_impl::input::TouchInjector injector_impl(
      inspect::Node(), InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [](auto...) { return true; },
      /*inject=*/
      [](auto...) {},
      /*on_channel_closed=*/[] {});

  bool injection_callback_fired = false;
  // Inject nothing.
  injector->Inject({}, [&injection_callback_fired] { injection_callback_fired = true; });
  test_loop.RunUntilIdle();

  EXPECT_FALSE(injection_callback_fired);
  EXPECT_TRUE(error_callback_fired);
}

TEST(InjectorTest, ClientClosingChannel_ShouldTriggerCancelEvents_ForEachOngoingStream) {
  // Test loop to be able to control dispatch without having to create an entire test class
  // subclassing TestLoopFixture.
  async::TestLoop test_loop;

  // Set up an isolated Injector.
  DevicePtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  std::vector<uint32_t> cancelled_streams;
  scenic_impl::input::TouchInjector injector_impl(
      inspect::Node(), InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [](auto...) { return true; },
      /*inject=*/
      [&cancelled_streams](const scenic_impl::input::InternalTouchEvent& event, StreamId) {
        if (event.phase == scenic_impl::input::Phase::kCancel)
          cancelled_streams.push_back(event.pointer_id);
      },
      /*on_channel_closed=*/[] {});

  // Start three streams and end one.
  {
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_pointer_id(1);
    event.mutable_data()->pointer_sample().set_phase(Phase::ADD);
    std::vector<InjectionEvent> events;
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)}, [] {});
  }
  {
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_pointer_id(2);
    event.mutable_data()->pointer_sample().set_phase(Phase::ADD);
    std::vector<InjectionEvent> events;
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)}, [] {});
  }
  {
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_pointer_id(3);
    event.mutable_data()->pointer_sample().set_phase(Phase::ADD);
    std::vector<InjectionEvent> events;
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)}, [] {});
  }
  {
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_pointer_id(1);
    event.mutable_data()->pointer_sample().set_phase(Phase::REMOVE);
    std::vector<InjectionEvent> events;
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)}, [] {});
  }

  // Close the client side channel.
  injector = {};
  test_loop.RunUntilIdle();

  // Should receive two CANCEL events, since there should be two ongoing streams.
  EXPECT_FALSE(error_callback_fired);
  EXPECT_THAT(cancelled_streams, testing::UnorderedElementsAre(2, 3));
}

TEST(InjectorTest, ServerClosingChannel_ShouldTriggerCancelEvents_ForEachOngoingStream) {
  // Test loop to be able to control dispatch without having to create an entire test class
  // subclassing TestLoopFixture.
  async::TestLoop test_loop;

  // Set up an isolated Injector.
  DevicePtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  std::vector<uint32_t> cancelled_streams;
  scenic_impl::input::TouchInjector injector_impl(
      inspect::Node(), InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [](auto...) { return true; },
      /*inject=*/
      [&cancelled_streams](const scenic_impl::input::InternalTouchEvent& event, StreamId) {
        if (event.phase == scenic_impl::input::Phase::kCancel)
          cancelled_streams.push_back(event.pointer_id);
      },
      /*on_channel_closed=*/[] {});

  // Start three streams and end one.
  {
    std::vector<InjectionEvent> events;
    {
      InjectionEvent event = InjectionEventTemplate();
      event.mutable_data()->pointer_sample().set_pointer_id(1);
      event.mutable_data()->pointer_sample().set_phase(Phase::ADD);
      events.emplace_back(std::move(event));
    }
    {
      InjectionEvent event = InjectionEventTemplate();
      event.mutable_data()->pointer_sample().set_pointer_id(2);
      event.mutable_data()->pointer_sample().set_phase(Phase::ADD);
      events.emplace_back(std::move(event));
    }
    {
      InjectionEvent event = InjectionEventTemplate();
      event.mutable_data()->pointer_sample().set_pointer_id(3);
      event.mutable_data()->pointer_sample().set_phase(Phase::ADD);
      events.emplace_back(std::move(event));
    }
    {
      InjectionEvent event = InjectionEventTemplate();
      event.mutable_data()->pointer_sample().set_pointer_id(1);
      event.mutable_data()->pointer_sample().set_phase(Phase::REMOVE);
      events.emplace_back(std::move(event));
    }
    injector->Inject({std::move(events)}, [] {});
  }

  // Inject an event with missing fields to cause the channel to close.
  {
    std::vector<InjectionEvent> events;
    events.emplace_back();
    injector->Inject(std::move(events), [] {});
  }
  test_loop.RunUntilIdle();

  EXPECT_TRUE(error_callback_fired);
  // Should receive CANCEL events for the two ongoing streams; 2 and 3.
  EXPECT_THAT(cancelled_streams, testing::UnorderedElementsAre(2, 3));
}

TEST(InjectorTest, InjectionOfEmptyEvent_ShouldCloseChannel) {
  // Test loop to be able to control dispatch without having to create an entire test class
  // subclassing TestLoopFixture.
  async::TestLoop test_loop;

  // Set up an isolated Injector.
  DevicePtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](auto) { error_callback_fired = true; });

  bool injection_lambda_fired = false;
  scenic_impl::input::TouchInjector injector_impl(
      inspect::Node(), InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [](zx_koid_t, zx_koid_t) { return true; },
      /*inject=*/
      [&injection_lambda_fired](auto...) { injection_lambda_fired = true; },
      /*on_channel_closed=*/[] {});

  bool injection_callback_fired = false;
  InjectionEvent event;
  std::vector<InjectionEvent> events;
  events.emplace_back(std::move(event));
  injector->Inject({std::move(events)},
                   [&injection_callback_fired] { injection_callback_fired = true; });
  test_loop.RunUntilIdle();

  EXPECT_FALSE(injection_lambda_fired);
  EXPECT_FALSE(injection_callback_fired);
  EXPECT_TRUE(error_callback_fired);
}

TEST(InjectorTest, ClientClosingChannel_ShouldTriggerOnChannelClosedLambda) {
  // Test loop to be able to control dispatch without having to create an entire test class
  // subclassing TestLoopFixture.
  async::TestLoop test_loop;

  // Set up an isolated Injector.
  DevicePtr injector;

  bool client_error_callback_fired = false;
  injector.set_error_handler(
      [&client_error_callback_fired](zx_status_t) { client_error_callback_fired = true; });

  bool on_channel_closed_callback_fired = false;
  scenic_impl::input::TouchInjector injector_impl(
      inspect::Node(), InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/[](auto...) { return true; },
      /*inject=*/[](auto...) {},
      /*on_channel_closed=*/
      [&on_channel_closed_callback_fired] { on_channel_closed_callback_fired = true; });

  // Close the client side channel.
  injector = {};
  test_loop.RunUntilIdle();

  EXPECT_FALSE(client_error_callback_fired);
  EXPECT_TRUE(on_channel_closed_callback_fired);
}

TEST(InjectorTest, ServerClosingChannel_ShouldTriggerOnChannelClosedLambda) {
  // Test loop to be able to control dispatch without having to create an entire test class
  // subclassing TestLoopFixture.
  async::TestLoop test_loop;

  // Set up an isolated Injector.
  DevicePtr injector;

  bool client_error_callback_fired = false;
  injector.set_error_handler(
      [&client_error_callback_fired](zx_status_t) { client_error_callback_fired = true; });

  bool on_channel_closed_callback_fired = false;
  scenic_impl::input::TouchInjector injector_impl(
      inspect::Node(), InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/[](auto...) { return true; },
      /*inject=*/[](auto...) {},
      /*on_channel_closed=*/
      [&on_channel_closed_callback_fired] { on_channel_closed_callback_fired = true; });

  // Inject an event with missing fields to cause the channel to close.
  {
    std::vector<InjectionEvent> events;
    events.emplace_back();
    injector->Inject(std::move(events), [] {});
  }
  test_loop.RunUntilIdle();

  EXPECT_TRUE(client_error_callback_fired);
  EXPECT_TRUE(on_channel_closed_callback_fired);
}

// Test for lazy connectivity detection.
TEST(InjectorTest, InjectionWithBadConnectivity_ShouldCloseChannel) {
  // Test loop to be able to control dispatch without having to create an entire test class
  // subclassing TestLoopFixture.
  async::TestLoop test_loop;

  // Set up an isolated Injector.
  DevicePtr injector;

  bool error_callback_fired = false;
  zx_status_t error = ZX_OK;
  injector.set_error_handler([&error_callback_fired, &error](zx_status_t status) {
    error_callback_fired = true;
    error = status;
  });

  bool connectivity_is_good = true;
  uint32_t num_cancel_events = 0;
  scenic_impl::input::TouchInjector injector_impl(
      inspect::Node(), InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [&connectivity_is_good](zx_koid_t, zx_koid_t) { return connectivity_is_good; },
      /*inject=*/
      [&num_cancel_events](const scenic_impl::input::InternalTouchEvent& event, StreamId) {
        num_cancel_events += event.phase == scenic_impl::input::Phase::kCancel ? 1 : 0;
      },
      /*on_channel_closed=*/[] {});

  // Start event stream while connectivity is good.
  {
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_phase(Phase::ADD);
    event.mutable_data()->pointer_sample().set_pointer_id(1);
    std::vector<InjectionEvent> events;
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)}, [] {});
    test_loop.RunUntilIdle();
  }

  // Connectivity was good. No problems.
  EXPECT_FALSE(error_callback_fired);

  // Inject with bad connectivity.
  connectivity_is_good = false;
  {
    bool injection_callback_fired = false;
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_phase(Phase::CHANGE);
    std::vector<InjectionEvent> events;
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)},
                     [&injection_callback_fired] { injection_callback_fired = true; });
    test_loop.RunUntilIdle();
    EXPECT_FALSE(injection_callback_fired);
  }

  // Connectivity was bad, so channel should be closed and an extra CANCEL event should have been
  // injected for each ongoing stream.
  EXPECT_EQ(num_cancel_events, 1u);
  EXPECT_TRUE(error_callback_fired);
  EXPECT_EQ(error, ZX_ERR_BAD_STATE);
}

// Class for testing parameterized injection of invalid events.
// Takes an int that determines which field gets deleted (parameter must be copyable).
class InjectorInvalidEventsTest : public gtest::TestLoopFixture,
                                  public testing::WithParamInterface<int> {};

INSTANTIATE_TEST_SUITE_P(InjectEventWithMissingField_ShouldCloseChannel, InjectorInvalidEventsTest,
                         testing::Range(0, 3));

TEST_P(InjectorInvalidEventsTest, InjectEventWithMissingField_ShouldCloseChannel) {
  // Create event with a missing field based on GetParam().
  InjectionEvent event = InjectionEventTemplate();
  switch (GetParam()) {
    case 0:
      event.mutable_data()->pointer_sample().clear_pointer_id();
      break;
    case 1:
      event.mutable_data()->pointer_sample().clear_phase();
      break;
    case 2:
      event.mutable_data()->pointer_sample().clear_position_in_viewport();
      break;
  }

  // Set up an isolated Injector.
  DevicePtr injector;

  bool error_callback_fired = false;
  zx_status_t error = ZX_OK;
  injector.set_error_handler([&error_callback_fired, &error](zx_status_t status) {
    error_callback_fired = true;
    error = status;
  });

  scenic_impl::input::TouchInjector injector_impl(
      inspect::Node(), InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [](auto...) { return true; },
      /*inject=*/
      [](auto...) {},
      /*on_channel_closed=*/[] {});

  bool injection_callback_fired = false;
  std::vector<InjectionEvent> events;
  events.emplace_back(std::move(event));
  injector->Inject({std::move(events)},
                   [&injection_callback_fired] { injection_callback_fired = true; });
  RunLoopUntilIdle();

  EXPECT_FALSE(injection_callback_fired);
  EXPECT_TRUE(error_callback_fired);
  EXPECT_EQ(error, ZX_ERR_INVALID_ARGS);
}

// Class for testing different event streams.
// Each invocation gets a vector of pairs of pointer ids and Phases, representing pointer streams.
class InjectorGoodEventStreamTest
    : public gtest::TestLoopFixture,
      public testing::WithParamInterface<std::vector<std::pair</*pointer_id*/ uint32_t, Phase>>> {};

static std::vector<std::vector<std::pair<uint32_t, Phase>>> GoodStreamTestData() {
  // clang-format off
  return {
    {{1, Phase::ADD}, {1, Phase::REMOVE}},                         // 0: one pointer trivial
    {{1, Phase::ADD}, {1, Phase::CHANGE}, {1, Phase::REMOVE}},     // 1: one pointer minimal all phases
    {{1, Phase::ADD}, {1, Phase::CANCEL}},                         // 2: one pointer trivial cancelled
    {{1, Phase::ADD}, {1, Phase::CHANGE}, {1, Phase::CANCEL}},     // 3: one pointer minimal all phases cancelled
    {{1, Phase::ADD}, {1, Phase::CHANGE}, {1, Phase::CANCEL},
     {2, Phase::ADD}, {2, Phase::CHANGE}, {2, Phase::CANCEL}},     // 4: two pointers successive streams
    {{2, Phase::ADD},    {1, Phase::ADD},    {2, Phase::CHANGE},
     {1, Phase::CHANGE}, {1, Phase::CANCEL}, {2, Phase::CANCEL}},  // 5: two pointer interleaved
  };
  // clang-format on
}

INSTANTIATE_TEST_SUITE_P(InjectionWithGoodEventStream_ShouldHaveNoProblems_CombinedEvents,
                         InjectorGoodEventStreamTest, testing::ValuesIn(GoodStreamTestData()));

INSTANTIATE_TEST_SUITE_P(InjectionWithGoodEventStream_ShouldHaveNoProblems_SeparateEvents,
                         InjectorGoodEventStreamTest, testing::ValuesIn(GoodStreamTestData()));

// Inject a valid event stream in a single Inject() call.
TEST_P(InjectorGoodEventStreamTest,
       InjectionWithGoodEventStream_ShouldHaveNoProblems_CombinedEvents) {
  // Set up an isolated Injector.
  DevicePtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  scenic_impl::input::TouchInjector injector_impl(
      inspect::Node(), InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [](auto...) { return true; },  // Always true.
      /*inject=*/
      [](auto...) {},
      /*on_channel_closed=*/[] {});

  std::vector<InjectionEvent> events;
  for (auto [pointer_id, phase] : GetParam()) {
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_pointer_id(pointer_id);
    event.mutable_data()->pointer_sample().set_phase(phase);
    events.emplace_back(std::move(event));
  }

  bool injection_callback_fired = false;
  injector->Inject({std::move(events)},
                   [&injection_callback_fired] { injection_callback_fired = true; });
  RunLoopUntilIdle();

  EXPECT_TRUE(injection_callback_fired);
  EXPECT_FALSE(error_callback_fired);
}

// Inject a valid event stream in multiple Inject() calls.
TEST_P(InjectorGoodEventStreamTest,
       InjectionWithGoodEventStream_ShouldHaveNoProblems_SeparateEvents) {
  // Set up an isolated Injector.
  DevicePtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  scenic_impl::input::TouchInjector injector_impl(
      inspect::Node(), InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [](auto...) { return true; },  // Always true.
      /*inject=*/
      [](auto...) {},
      /*on_channel_closed=*/[] {});

  for (auto [pointer_id, phase] : GetParam()) {
    bool injection_callback_fired = false;
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_pointer_id(pointer_id);
    event.mutable_data()->pointer_sample().set_phase(phase);
    std::vector<InjectionEvent> events;
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)},
                     [&injection_callback_fired] { injection_callback_fired = true; });
    RunLoopUntilIdle();

    EXPECT_TRUE(injection_callback_fired);
    ASSERT_FALSE(error_callback_fired);
  }
}

// Bad event streams.
// Each invocation gets a vector of pairs of pointer ids and Phases, representing pointer streams.
class InjectorBadEventStreamTest
    : public gtest::TestLoopFixture,
      public testing::WithParamInterface<std::vector<std::pair</*pointer_id*/ uint32_t, Phase>>> {};

static std::vector<std::vector<std::pair<uint32_t, Phase>>> BadStreamTestData() {
  // clang-format off
  return {
    {{1, Phase::CHANGE}},                                       // 0: one pointer non-add initial event
    {{1, Phase::REMOVE}},                                       // 1: one pointer non-add initial event
    {{1, Phase::ADD}, {1, Phase::ADD}},                         // 2: one pointer double add
    {{1, Phase::ADD}, {1, Phase::CHANGE}, {1, Phase::ADD}},     // 3: one pointer double add mid-stream
    {{1, Phase::ADD}, {1, Phase::REMOVE}, {1, Phase::REMOVE}},  // 4: one pointer double remove
    {{1, Phase::ADD}, {1, Phase::REMOVE}, {1, Phase::CHANGE}},  // 5: one pointer event after remove
    {{1, Phase::ADD}, {1, Phase::CHANGE},
     {1, Phase::REMOVE}, {2, Phase::ADD}, {2, Phase::ADD}},     // 6: two pointer faulty stream after correct stream
    {{1, Phase::ADD}, {2, Phase::ADD},
     {2, Phase::CHANGE}, {2, Phase::REMOVE}, {1, Phase::ADD}},  // 7  two pointer faulty stream interleaved with correct stream
  };
  // clang-format on
}

INSTANTIATE_TEST_SUITE_P(InjectionWithBadEventStream_ShouldCloseChannel_CombinedEvents,
                         InjectorBadEventStreamTest, testing::ValuesIn(BadStreamTestData()));

INSTANTIATE_TEST_SUITE_P(InjectionWithBadEventStream_ShouldCloseChannel_SeparateEvents,
                         InjectorBadEventStreamTest, testing::ValuesIn(BadStreamTestData()));

// Inject an invalid event stream in a single Inject() call.
TEST_P(InjectorBadEventStreamTest, InjectionWithBadEventStream_ShouldCloseChannel_CombinedEvents) {
  // Set up an isolated Injector.
  DevicePtr injector;

  bool error_callback_fired = false;
  zx_status_t error = ZX_OK;
  injector.set_error_handler([&error_callback_fired, &error](zx_status_t status) {
    error_callback_fired = true;
    error = status;
  });

  scenic_impl::input::TouchInjector injector_impl(
      inspect::Node(), InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/[](auto...) { return true; },
      /*inject=*/[](auto...) {},
      /*on_channel_closed=*/[] {});

  InjectionEvent event = InjectionEventTemplate();

  // Run event stream.
  std::vector<InjectionEvent> events;
  for (auto [pointer_id, phase] : GetParam()) {
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_pointer_id(pointer_id);
    event.mutable_data()->pointer_sample().set_phase(phase);
    events.emplace_back(std::move(event));
  }
  injector->Inject({std::move(events)}, [] {});
  RunLoopUntilIdle();

  EXPECT_TRUE(error_callback_fired);
  EXPECT_EQ(error, ZX_ERR_BAD_STATE);
}

// Inject an invalid event stream in multiple Inject() calls.
TEST_P(InjectorBadEventStreamTest, InjectionWithBadEventStream_ShouldCloseChannel_SeparateEvents) {
  // Set up an isolated Injector.
  DevicePtr injector;

  bool error_callback_fired = false;
  zx_status_t error = ZX_OK;
  injector.set_error_handler([&error_callback_fired, &error](zx_status_t status) {
    error_callback_fired = true;
    error = status;
  });

  scenic_impl::input::TouchInjector injector_impl(
      inspect::Node(), InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/[](auto...) { return true; },
      /*inject=*/[](auto...) {},
      /*on_channel_closed=*/[] {});

  // Run event stream.
  for (auto [pointer_id, phase] : GetParam()) {
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_pointer_id(pointer_id);
    event.mutable_data()->pointer_sample().set_phase(phase);
    std::vector<InjectionEvent> events;
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)}, [] {});
    RunLoopUntilIdle();
  }

  EXPECT_TRUE(error_callback_fired);
  EXPECT_EQ(error, ZX_ERR_BAD_STATE);
}

TEST(InjectorTest, InjectedViewport_ShouldNotTriggerInjectLambda) {
  async::TestLoop test_loop;

  // Set up an isolated Injector.
  DevicePtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  bool inject_lambda_fired = false;
  scenic_impl::input::TouchInjector injector_impl(
      inspect::Node(), InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/[](zx_koid_t, zx_koid_t) { return true; },
      /*inject=*/[&inject_lambda_fired](auto...) { inject_lambda_fired = true; },
      /*on_channel_closed=*/[] {});

  {
    bool injection_callback_fired = false;
    InjectionEvent event;
    event.set_timestamp(1);
    {
      fuchsia::ui::pointerinjector::Viewport viewport;
      viewport.set_extents({{{-242, -383}, {124, 252}}});
      viewport.set_viewport_to_context_transform(kIdentityMatrix);
      fuchsia::ui::pointerinjector::Data data;
      data.set_viewport(std::move(viewport));
      event.set_data(std::move(data));
    }

    std::vector<InjectionEvent> events;
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)},
                     [&injection_callback_fired] { injection_callback_fired = true; });
    test_loop.RunUntilIdle();
    EXPECT_TRUE(injection_callback_fired);
  }

  test_loop.RunUntilIdle();

  EXPECT_FALSE(inject_lambda_fired);
  EXPECT_FALSE(error_callback_fired);
}

// Parameterized tests for malformed viewport arguments.
// Use pairs of optional extents and matrices. Because test parameters must be copyable.
using ViewportPair = std::pair<std::optional<std::array<std::array<float, 2>, 2>>,
                               std::optional<std::array<float, 9>>>;
class InjectorBadViewportTest : public gtest::TestLoopFixture,
                                public testing::WithParamInterface<ViewportPair> {};

static std::vector<ViewportPair> BadViewportTestData() {
  std::vector<ViewportPair> bad_viewports;
  {  // 0: No extents.
    ViewportPair pair;
    pair.second.emplace(kIdentityMatrix);
    bad_viewports.emplace_back(pair);
  }
  {  // 1: No viewport_to_context_transform.
    ViewportPair pair;
    pair.first = {{/*min*/ {0, 0}, /*max*/ {10, 10}}};
    bad_viewports.emplace_back(pair);
  }
  {  // 2: Malformed extents: Min bigger than max.
    fuchsia::ui::pointerinjector::Viewport viewport;
    ViewportPair pair;
    pair.first = {{/*min*/ {-100, 100}, /*max*/ {100, -100}}};
    pair.second = kIdentityMatrix;
    bad_viewports.emplace_back(pair);
  }
  {  // 3: Malformed extents: Min equal to max.
    fuchsia::ui::pointerinjector::Viewport viewport;
    ViewportPair pair;
    pair.first = {{/*min*/ {0, -100}, /*max*/ {0, 100}}};
    pair.second = kIdentityMatrix;
    bad_viewports.emplace_back(pair);
  }
  {  // 4: Malformed extents: Contains NaN
    fuchsia::ui::pointerinjector::Viewport viewport;
    ViewportPair pair;
    pair.first = {{/*min*/ {0, 0}, /*max*/ {100, std::numeric_limits<double>::quiet_NaN()}}};
    pair.second = kIdentityMatrix;
    bad_viewports.emplace_back(pair);
  }
  {  // 5: Malformed extents: Contains Inf
    fuchsia::ui::pointerinjector::Viewport viewport;
    ViewportPair pair;
    pair.first = {{/*min*/ {0, 0}, /*max*/ {100, std::numeric_limits<double>::infinity()}}};
    pair.second = kIdentityMatrix;
    bad_viewports.emplace_back(pair);
  }
  {  // 6: Malformed transform: Non-invertible matrix
    // clang-format off
    const std::array<float, 9> non_invertible_matrix = {
      1, 0, 0,
      1, 0, 0,
      0, 0, 1,
    };
    // clang-format on
    fuchsia::ui::pointerinjector::Viewport viewport;
    ViewportPair pair;
    pair.first = {{{/*min*/ {0, 0}, /*max*/ {10, 10}}}};
    pair.second = non_invertible_matrix;
    bad_viewports.emplace_back(pair);
  }
  {  // 7: Malformed transform: Contains NaN
    // clang-format off
    const std::array<float, 9> nan_matrix = {
      1, std::numeric_limits<double>::quiet_NaN(), 0,
      0, 1, 0,
      0, 0, 1,
    };
    // clang-format on
    fuchsia::ui::pointerinjector::Viewport viewport;
    ViewportPair pair;
    pair.first = {{{/*min*/ {0, 0}, /*max*/ {10, 10}}}};
    pair.second = nan_matrix;
    bad_viewports.emplace_back(pair);
  }
  {  // 8: Malformed transform: Contains Inf
    // clang-format off
    const std::array<float, 9> inf_matrix = {
      1, std::numeric_limits<double>::infinity(), 0,
      0, 1, 0,
      0, 0, 1,
    };
    // clang-format on
    fuchsia::ui::pointerinjector::Viewport viewport;
    ViewportPair pair;
    pair.first = {{{/*min*/ {0, 0}, /*max*/ {10, 10}}}};
    pair.second = inf_matrix;
    bad_viewports.emplace_back(pair);
  }

  return bad_viewports;
}

INSTANTIATE_TEST_SUITE_P(InjectBadViewport_ShouldCloseChannel, InjectorBadViewportTest,
                         testing::ValuesIn(BadViewportTestData()));

TEST_P(InjectorBadViewportTest, InjectBadViewport_ShouldCloseChannel) {
  DevicePtr injector;
  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  bool inject_lambda_fired = false;
  scenic_impl::input::TouchInjector injector_impl(
      inspect::Node(), InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/[](zx_koid_t, zx_koid_t) { return true; },
      /*inject=*/[&inject_lambda_fired](auto...) { inject_lambda_fired = true; },
      /*on_channel_closed=*/[] {});

  InjectionEvent event;
  {
    event.set_timestamp(1);
    fuchsia::ui::pointerinjector::Data data;
    ViewportPair params = GetParam();
    fuchsia::ui::pointerinjector::Viewport viewport;
    if (params.first)
      viewport.set_extents(params.first.value());
    if (params.second)
      viewport.set_viewport_to_context_transform(params.second.value());
    data.set_viewport(std::move(viewport));
    event.set_data(std::move(data));
  }

  std::vector<InjectionEvent> events;
  events.emplace_back(std::move(event));
  bool injection_callback_fired = false;
  injector->Inject({std::move(events)},
                   [&injection_callback_fired] { injection_callback_fired = true; });

  RunLoopUntilIdle();
  EXPECT_FALSE(injection_callback_fired);
  EXPECT_TRUE(error_callback_fired);
}

class InjectorInspectionTest : public gtest::TestLoopFixture {
 protected:
  void SetUp() override {
    injector_impl_.emplace(
        inspector_.GetRoot().CreateChild("injector"), InjectorSettingsTemplate(),
        ViewportTemplate(), injector_.NewRequest(),
        /*is_descendant_and_connected=*/[](auto...) { return true; },
        /*inject=*/[this](auto...) { ++num_injections_; },
        /*on_channel_closed=*/[] {});
  }

  std::pair<inspect::Hierarchy, const inspect::Hierarchy*> ReadHierarchyFromInspector() {
    fpromise::result<inspect::Hierarchy> result;
    fpromise::single_threaded_executor exec;
    exec.schedule_task(
        inspect::ReadFromInspector(inspector_).then([&](fpromise::result<inspect::Hierarchy>& res) {
          result = std::move(res);
        }));
    exec.run();

    inspect::Hierarchy root = result.take_value();
    const inspect::Hierarchy* hierarchy = root.GetByPath({"injector"});
    FX_DCHECK(hierarchy);
    return {std::move(root), hierarchy};
  }

  std::vector<inspect::UintArrayValue::HistogramBucket> GetHistogramBuckets(
      const std::string& property) {
    const auto [root, parent] = ReadHierarchyFromInspector();
    const inspect::UintArrayValue* histogram =
        parent->node().get_property<inspect::UintArrayValue>(property);
    FX_CHECK(histogram) << "no histogram named " << property << " found";
    return histogram->GetBuckets();
  }

  uint64_t GetInjectionsAtMinute(uint64_t minute) {
    const auto [root, parent] = ReadHierarchyFromInspector();
    const inspect::Hierarchy* node = parent->GetByPath({kHistoryNodeName});
    FX_CHECK(node);
    const inspect::UintPropertyValue* count = node->node().get_property<inspect::UintPropertyValue>(
        "Events at minute " + std::to_string(minute));
    if (count) {
      return count->value();
    } else {
      FX_LOGS(INFO) << "Found no data for minute " << minute;
      ;
      return 0;
    }
  }

  uint64_t GetTotalInjections() {
    const auto [root, parent] = ReadHierarchyFromInspector();
    const inspect::Hierarchy* node = parent->GetByPath({kHistoryNodeName});
    FX_CHECK(node);
    const inspect::UintPropertyValue* total =
        node->node().get_property<inspect::UintPropertyValue>("Total");
    FX_CHECK(total);
    return total->value();
  }

  const std::string kHistoryNodeName =
      "Last " + std::to_string(scenic_impl::input::InjectorInspector::kNumMinutesOfHistory) +
      " minutes of injected events";
  inspect::Inspector inspector_;
  DevicePtr injector_;
  uint64_t num_injections_ = 0;
  std::optional<scenic_impl::input::TouchInjector> injector_impl_;
};

TEST_F(InjectorInspectionTest, HistogramsTrackInjections) {
  bool error_callback_fired = false;
  injector_.set_error_handler(
      [&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  {  // Inject ADD event.
    bool injection_callback_fired = false;
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_phase(Phase::ADD);
    std::vector<InjectionEvent> events;
    events.emplace_back(std::move(event));
    injector_->Inject({std::move(events)},
                      [&injection_callback_fired] { injection_callback_fired = true; });
    RunLoopUntilIdle();
    EXPECT_TRUE(injection_callback_fired);

    EXPECT_EQ(num_injections_, 1u);
    EXPECT_FALSE(error_callback_fired);
  }

  {  // Inject CHANGE event.
    bool injection_callback_fired = false;
    std::vector<InjectionEvent> events;
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_phase(Phase::CHANGE);
    events.emplace_back(std::move(event));
    injector_->Inject({std::move(events)},
                      [&injection_callback_fired] { injection_callback_fired = true; });
    RunLoopUntilIdle();
    EXPECT_TRUE(injection_callback_fired);

    EXPECT_EQ(num_injections_, 2u);
    EXPECT_FALSE(error_callback_fired);
  }

  {  // Inject REMOVE event.
    bool injection_callback_fired = false;
    std::vector<InjectionEvent> events;
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_phase(Phase::REMOVE);
    events.emplace_back(std::move(event));
    injector_->Inject({std::move(events)},
                      [&injection_callback_fired] { injection_callback_fired = true; });
    RunLoopUntilIdle();
    EXPECT_TRUE(injection_callback_fired);

    EXPECT_EQ(num_injections_, 3u);
    EXPECT_FALSE(error_callback_fired);
  }

  {  // Inject VIEWPORT event.
    bool injection_callback_fired = false;
    InjectionEvent event;
    event.set_timestamp(1);
    {
      fuchsia::ui::pointerinjector::Viewport viewport;
      viewport.set_extents({{{-242, -383}, {124, 252}}});
      viewport.set_viewport_to_context_transform(kIdentityMatrix);
      fuchsia::ui::pointerinjector::Data data;
      data.set_viewport(std::move(viewport));
      event.set_data(std::move(data));
    }

    std::vector<InjectionEvent> events;
    events.emplace_back(std::move(event));
    injector_->Inject({std::move(events)},
                      [&injection_callback_fired] { injection_callback_fired = true; });
    RunLoopUntilIdle();
    EXPECT_TRUE(injection_callback_fired);

    // Still 3 injections; the callback is not invoked for viewport changes.
    EXPECT_EQ(num_injections_, 3u);
    EXPECT_FALSE(error_callback_fired);
  }

  {
    uint64_t count = 0;
    for (const inspect::UintArrayValue::HistogramBucket& bucket :
         GetHistogramBuckets("viewport_event_latency_usecs")) {
      count += bucket.count;
    }

    EXPECT_EQ(count, 1u);
  }

  {
    uint64_t count = 0;
    for (const inspect::UintArrayValue::HistogramBucket& bucket :
         GetHistogramBuckets("pointer_event_latency_usecs")) {
      count += bucket.count;
    }

    EXPECT_EQ(count, 3u);
  }
}

TEST_F(InjectorInspectionTest, InspectHistory) {
  const uint64_t kMaxNum = scenic_impl::input::InjectorInspector::kNumMinutesOfHistory;
  ASSERT_TRUE(kMaxNum > 2) << "This test assumes a minimum length of history";

  const uint64_t start_minute = Now().get() / zx::min(1).get();

  bool error_callback_fired = false;
  injector_.set_error_handler(
      [&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  EXPECT_EQ(GetInjectionsAtMinute(start_minute), 0u);
  EXPECT_EQ(GetTotalInjections(), 0u);

  // Inject events. Each one should register in inspect.
  {
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_phase(Phase::ADD);
    std::vector<InjectionEvent> events;
    events.emplace_back(std::move(event));
    injector_->Inject({std::move(events)}, [] {});
    RunLoopUntilIdle();
  }

  EXPECT_EQ(GetInjectionsAtMinute(start_minute), 1u);
  EXPECT_EQ(GetTotalInjections(), 1u);

  {
    std::vector<InjectionEvent> events;
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_phase(Phase::CHANGE);
    events.emplace_back(std::move(event));
    injector_->Inject({std::move(events)}, [] {});
    RunLoopUntilIdle();
  }

  EXPECT_EQ(GetInjectionsAtMinute(start_minute), 2u);
  EXPECT_EQ(GetTotalInjections(), 2u);

  {
    std::vector<InjectionEvent> events;
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_phase(Phase::CHANGE);
    events.emplace_back(std::move(event));
    injector_->Inject({std::move(events)}, [] {});
    RunLoopUntilIdle();
  }

  EXPECT_EQ(GetInjectionsAtMinute(start_minute), 3u);
  EXPECT_EQ(GetTotalInjections(), 3u);

  {  // Inject VIEWPORT event. It should not be reflected in the injection stats.
    InjectionEvent event;
    event.set_timestamp(1);
    {
      fuchsia::ui::pointerinjector::Viewport viewport;
      viewport.set_extents({{{-242, -383}, {124, 252}}});
      viewport.set_viewport_to_context_transform(kIdentityMatrix);
      fuchsia::ui::pointerinjector::Data data;
      data.set_viewport(std::move(viewport));
      event.set_data(std::move(data));
    }

    std::vector<InjectionEvent> events;
    events.emplace_back(std::move(event));
    injector_->Inject({std::move(events)}, [] {});
    RunLoopUntilIdle();
  }

  EXPECT_EQ(GetInjectionsAtMinute(start_minute), 3u);
  EXPECT_EQ(GetTotalInjections(), 3u);

  // Roll forward one minute, inject an event and observe that history has updated correctly.
  RunLoopFor(zx::min(1));
  {
    std::vector<InjectionEvent> events;
    InjectionEvent event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_phase(Phase::CHANGE);
    events.emplace_back(std::move(event));
    injector_->Inject({std::move(events)}, [] {});
    RunLoopUntilIdle();
  }

  EXPECT_EQ(GetInjectionsAtMinute(start_minute), 3u);
  EXPECT_EQ(GetInjectionsAtMinute(start_minute + 1), 1u);
  EXPECT_EQ(GetTotalInjections(), 4u);

  // Roll forward one less than the size of the ringbuffer. Now the start minute should have
  // disappeared, but not the second minute.
  RunLoopFor(zx::min(kMaxNum - 1));

  EXPECT_EQ(GetInjectionsAtMinute(start_minute), 0u);
  EXPECT_EQ(GetInjectionsAtMinute(start_minute + 1), 1u);
  EXPECT_EQ(GetInjectionsAtMinute(start_minute + kMaxNum), 0u);
  EXPECT_EQ(GetTotalInjections(), 1u);

  {
    std::vector<InjectionEvent> events;
    {
      InjectionEvent event = InjectionEventTemplate();
      event.mutable_data()->pointer_sample().set_phase(Phase::CHANGE);
      events.emplace_back(std::move(event));
    }
    {
      InjectionEvent event = InjectionEventTemplate();
      event.mutable_data()->pointer_sample().set_phase(Phase::CHANGE);
      events.emplace_back(std::move(event));
    }
    injector_->Inject({std::move(events)}, [] {});
    RunLoopUntilIdle();
  }

  EXPECT_EQ(GetInjectionsAtMinute(start_minute), 0u);
  EXPECT_EQ(GetInjectionsAtMinute(start_minute + 1), 1u);
  EXPECT_EQ(GetInjectionsAtMinute(start_minute + kMaxNum), 2u);
  EXPECT_EQ(GetTotalInjections(), 3u);
}

}  // namespace input::test
