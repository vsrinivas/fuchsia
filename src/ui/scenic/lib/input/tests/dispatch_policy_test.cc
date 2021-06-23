// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/pointer/cpp/fidl.h>
#include <lib/async-testing/test_loop.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>

#include <gtest/gtest.h>

#include "lib/gtest/test_loop_fixture.h"
#include "src/ui/scenic/lib/input/input_system.h"
#include "src/ui/scenic/lib/utils/helpers.h"

// These tests exercise input event delivery under different dispatch policies.
// Setup:
// - Injection done in context View Space, with fuchsia.ui.pointerinjector
// - Target(s) specified by ViewRef
// - Dispatch done in fuchsia.ui.pointer

// All tests in this suite uses the following ViewTree layout:
//  Root
//    |
// Client1
//    |
// Client2
//    |     \
// Client4 Client3

namespace input::test {

using scenic_impl::input::Phase;
using scenic_impl::input::TouchSource;

constexpr float kDisplayWidth = 9.f;
constexpr float kDisplayHeight = 9.f;

class DispatchPolicyTest : public gtest::TestLoopFixture {
 public:
  DispatchPolicyTest()
      : input_system_(
            scenic_impl::SystemContext(context_provider_.context(), inspect::Node(), [] {}),
            fxl::WeakPtr<scenic_impl::gfx::SceneGraph>(), /*request_focus*/ [](auto...) {}) {}

  void SetUp() override {
    root_vrp_ = scenic::ViewRefPair::New();
    client1_vrp_ = scenic::ViewRefPair::New();
    client2_vrp_ = scenic::ViewRefPair::New();
    client3_vrp_ = scenic::ViewRefPair::New();
    client4_vrp_ = scenic::ViewRefPair::New();

    client1_ptr_.set_error_handler([](auto) { FAIL() << "Client1's channel closed"; });
    client2_ptr_.set_error_handler([](auto) { FAIL() << "Client2's channel closed"; });
    client3_ptr_.set_error_handler([](auto) { FAIL() << "Client3's channel closed"; });
    client4_ptr_.set_error_handler([](auto) { FAIL() << "Client4's channel closed"; });

    input_system_.RegisterTouchSource(client1_ptr_.NewRequest(), Client1Koid());
    input_system_.RegisterTouchSource(client2_ptr_.NewRequest(), Client2Koid());
    input_system_.RegisterTouchSource(client3_ptr_.NewRequest(), Client3Koid());
    input_system_.RegisterTouchSource(client4_ptr_.NewRequest(), Client4Koid());
  }

  void RegisterInjector(fuchsia::ui::views::ViewRef context_view_ref,
                        fuchsia::ui::views::ViewRef target_view_ref,
                        fuchsia::ui::pointerinjector::DispatchPolicy dispatch_policy,
                        fuchsia::ui::pointerinjector::DeviceType type) {
    fuchsia::ui::pointerinjector::Config config;
    config.set_device_id(1);
    config.set_device_type(type);
    config.set_dispatch_policy(dispatch_policy);
    {
      fuchsia::ui::pointerinjector::Viewport viewport;
      viewport.set_extents({{/*min*/ {0.f, 0.f}, /*max*/ {kDisplayWidth, kDisplayHeight}}});
      viewport.set_viewport_to_context_transform(
          // clang-format off
      {
        1.f, 0.f, 0.f, // first column
        0.f, 1.f, 0.f, // second column
        0.f, 0.f, 1.f, // third column
      }  // clang-format on
      );
      config.set_viewport(std::move(viewport));
    }
    {
      fuchsia::ui::pointerinjector::Context context;
      context.set_view(std::move(context_view_ref));
      config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerinjector::Target target;
      target.set_view(std::move(target_view_ref));
      config.set_target(std::move(target));
    }

    bool error_callback_fired = false;
    injector_.set_error_handler([&error_callback_fired](zx_status_t) {
      FX_LOGS(ERROR) << "Channel closed.";
      error_callback_fired = true;
    });
    bool register_callback_fired = false;
    input_system_.RegisterPointerinjector(
        std::move(config), injector_.NewRequest(),
        [&register_callback_fired] { register_callback_fired = true; });
    RunLoopUntilIdle();
    ASSERT_TRUE(register_callback_fired);
    ASSERT_FALSE(error_callback_fired);
  }

  // Creates a new snapshot with a hit test that returns |hits|, and a ViewTree with layout:
  // Root
  //   |
  // Client1
  //   |
  // Client2
  //   |  \
  // Client4 Client3
  std::shared_ptr<view_tree::Snapshot> NewSnapshot(std::vector<zx_koid_t> hits) {
    auto snapshot = std::make_shared<view_tree::Snapshot>();
    auto& [root, view_tree, _1, _2] = *snapshot;
    root = RootKoid();
    view_tree[RootKoid()] = {.children = {Client1Koid()}};
    view_tree[Client1Koid()] = {.parent = RootKoid(), .children = {Client2Koid()}};
    view_tree[Client2Koid()] = {.parent = Client1Koid(),
                                .children = {Client3Koid(), Client4Koid()}};
    view_tree[Client3Koid()] = {.parent = Client2Koid()};
    view_tree[Client4Koid()] = {.parent = Client2Koid()};

    snapshot->hit_testers.emplace_back([hits = std::move(hits)](auto...) mutable {
      return view_tree::SubtreeHitTestResult{.hits = std::move(hits)};
    });

    return snapshot;
  }

  void Inject(fuchsia::ui::pointerinjector::EventPhase phase) {
    FX_CHECK(injector_);
    std::vector<fuchsia::ui::pointerinjector::Event> events;
    {
      fuchsia::ui::pointerinjector::Event event;
      event.set_timestamp(0);
      fuchsia::ui::pointerinjector::PointerSample pointer_sample;
      pointer_sample.set_pointer_id(1);
      pointer_sample.set_phase(phase);
      pointer_sample.set_position_in_viewport({kDisplayWidth / 2.f, kDisplayHeight / 2.f});
      fuchsia::ui::pointerinjector::Data data;
      data.set_pointer_sample(std::move(pointer_sample));
      event.set_data(std::move(data));
      events.emplace_back(std::move(event));
    }

    bool inject_callback_fired = false;
    injector_->Inject(std::move(events),
                      [&inject_callback_fired] { inject_callback_fired = true; });
    RunLoopUntilIdle();
    ASSERT_TRUE(inject_callback_fired);
  }

  fuchsia::ui::views::ViewRef RootViewRef() { return fidl::Clone(root_vrp_.view_ref); }
  fuchsia::ui::views::ViewRef Client1ViewRef() { return fidl::Clone(client1_vrp_.view_ref); }
  fuchsia::ui::views::ViewRef Client2ViewRef() { return fidl::Clone(client2_vrp_.view_ref); }
  fuchsia::ui::views::ViewRef Client3ViewRef() { return fidl::Clone(client3_vrp_.view_ref); }
  fuchsia::ui::views::ViewRef Client4ViewRef() { return fidl::Clone(client4_vrp_.view_ref); }

  zx_koid_t RootKoid() { return utils::ExtractKoid(root_vrp_.view_ref); }
  zx_koid_t Client1Koid() { return utils::ExtractKoid(client1_vrp_.view_ref); }
  zx_koid_t Client2Koid() { return utils::ExtractKoid(client2_vrp_.view_ref); }
  zx_koid_t Client3Koid() { return utils::ExtractKoid(client3_vrp_.view_ref); }
  zx_koid_t Client4Koid() { return utils::ExtractKoid(client4_vrp_.view_ref); }

 private:
  // Must be initialized before |input_system_|.
  sys::testing::ComponentContextProvider context_provider_;

 protected:
  scenic_impl::input::InputSystem input_system_;
  fuchsia::ui::pointerinjector::DevicePtr injector_;
  fuchsia::ui::pointer::TouchSourcePtr client1_ptr_;
  fuchsia::ui::pointer::TouchSourcePtr client2_ptr_;
  fuchsia::ui::pointer::TouchSourcePtr client3_ptr_;
  fuchsia::ui::pointer::TouchSourcePtr client4_ptr_;

 private:
  scenic::ViewRefPair root_vrp_;
  scenic::ViewRefPair client1_vrp_;
  scenic::ViewRefPair client2_vrp_;
  scenic::ViewRefPair client3_vrp_;
  scenic::ViewRefPair client4_vrp_;
};

TEST_F(DispatchPolicyTest, ExclusiveMode_ShouldDeliverTo_OnlyTarget) {
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(/*hits*/ {Client4Koid()}));

  {  // Scene is set up. Inject with Client2 as exclusive target.
    RegisterInjector(
        /*context=*/RootViewRef(),
        /*target=*/Client2ViewRef(), fuchsia::ui::pointerinjector::DispatchPolicy::EXCLUSIVE_TARGET,
        fuchsia::ui::pointerinjector::DeviceType::TOUCH);
    Inject(fuchsia::ui::pointerinjector::EventPhase::ADD);
    Inject(fuchsia::ui::pointerinjector::EventPhase::CHANGE);
    Inject(fuchsia::ui::pointerinjector::EventPhase::REMOVE);
    RunLoopUntilIdle();
  }

  {
    std::vector<fuchsia::ui::pointer::TouchEvent> events;
    client2_ptr_->Watch({}, [&events](auto in_events) { events = std::move(in_events); });
    RunLoopUntilIdle();
    EXPECT_EQ(events.size(), 3u);
  }

  {
    bool client1_callback_fired = false;
    client1_ptr_->Watch({}, [&client1_callback_fired](auto) { client1_callback_fired = true; });
    bool client3_callback_fired = false;
    client3_ptr_->Watch({}, [&client3_callback_fired](auto) { client3_callback_fired = true; });
    bool client4_callback_fired = false;
    client4_ptr_->Watch({}, [&client4_callback_fired](auto) { client4_callback_fired = true; });

    RunLoopUntilIdle();
    EXPECT_FALSE(client1_callback_fired);
    EXPECT_FALSE(client3_callback_fired);
    EXPECT_FALSE(client4_callback_fired);
  }
}

TEST_F(DispatchPolicyTest, TopHitMode_OnLeafTarget_ShouldDeliverTo_OnlyTarget) {
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(/*hits*/ {Client3Koid()}));

  {  // Inject with Client3 as target. Top hit is Client3.
    RegisterInjector(/*context=*/RootViewRef(),
                     /*target=*/Client3ViewRef(),
                     fuchsia::ui::pointerinjector::DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET,
                     fuchsia::ui::pointerinjector::DeviceType::TOUCH);
    Inject(fuchsia::ui::pointerinjector::EventPhase::ADD);
    Inject(fuchsia::ui::pointerinjector::EventPhase::CHANGE);
    Inject(fuchsia::ui::pointerinjector::EventPhase::REMOVE);
    RunLoopUntilIdle();
  }

  {  // Target should receive events.
    std::vector<fuchsia::ui::pointer::TouchEvent> events;
    client3_ptr_->Watch({}, [&events](auto in_events) { events = std::move(in_events); });
    RunLoopUntilIdle();
    EXPECT_EQ(events.size(), 3u);
  }

  {  // No other client should receive any events.
    bool client1_callback_fired = false;
    client1_ptr_->Watch({}, [&client1_callback_fired](auto) { client1_callback_fired = true; });
    bool client2_callback_fired = false;
    client2_ptr_->Watch({}, [&client2_callback_fired](auto) { client2_callback_fired = true; });
    bool client4_callback_fired = false;
    client4_ptr_->Watch({}, [&client4_callback_fired](auto) { client4_callback_fired = true; });

    RunLoopUntilIdle();
    EXPECT_FALSE(client1_callback_fired);
    EXPECT_FALSE(client2_callback_fired);
    EXPECT_FALSE(client4_callback_fired);
  }
}

TEST_F(DispatchPolicyTest,
       TopHitMode_OnMidTreeTarget_ShouldDeliverTo_TopHitAndAncestorsUpToTarget) {
  input_system_.OnNewViewTreeSnapshot(NewSnapshot(/*hits*/ {Client4Koid()}));

  {  // Inject with Client2 as target. Top hit is Client4.
    RegisterInjector(/*context=*/RootViewRef(),
                     /*target=*/Client2ViewRef(),
                     fuchsia::ui::pointerinjector::DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET,
                     fuchsia::ui::pointerinjector::DeviceType::TOUCH);
    Inject(fuchsia::ui::pointerinjector::EventPhase::ADD);
    Inject(fuchsia::ui::pointerinjector::EventPhase::CHANGE);
    Inject(fuchsia::ui::pointerinjector::EventPhase::REMOVE);
    RunLoopUntilIdle();
  }

  {  // Top hit should receive events.
    std::vector<fuchsia::ui::pointer::TouchEvent> events;
    client4_ptr_->Watch({}, [&events](auto in_events) { events = std::move(in_events); });
    RunLoopUntilIdle();
    EXPECT_EQ(events.size(), 3u);
  }
  {  // Target should receive events, since it's the only ancestor of top hit.
    std::vector<fuchsia::ui::pointer::TouchEvent> events;
    client2_ptr_->Watch({}, [&events](auto in_events) { events = std::move(in_events); });
    RunLoopUntilIdle();
    EXPECT_EQ(events.size(), 3u);
  }

  {  // No other client should receive any events.
    bool client1_callback_fired = false;
    client1_ptr_->Watch({}, [&client1_callback_fired](auto) { client1_callback_fired = true; });
    bool client3_callback_fired = false;
    client3_ptr_->Watch({}, [&client3_callback_fired](auto) { client3_callback_fired = true; });

    RunLoopUntilIdle();
    EXPECT_FALSE(client1_callback_fired);
    EXPECT_FALSE(client3_callback_fired);
  }
}

}  // namespace input::test
