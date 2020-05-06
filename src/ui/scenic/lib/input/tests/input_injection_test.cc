// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/scenic/lib/input/input_system.h"
#include "src/ui/scenic/lib/input/tests/util.h"

using fuchsia::ui::views::ViewRef;
using Phase = fuchsia::ui::input3::PointerEventPhase;

namespace lib_ui_input_tests {
namespace {

class InputInjectionTest : public InputSystemTest {
 public:
  InputInjectionTest() {}

  void TearDown() override {
    root_resources_.reset();
    root_session_.reset();
    parent_.reset();
    child_.reset();
    InputSystemTest::TearDown();
  }

  // Create a view tree of depth 3: scene, parent view, child view.
  // Return view refs of parent view and child view.
  std::pair<ViewRef, ViewRef> SetupSceneWithParentAndChildViews() {
    auto [v1, vh1] = scenic::ViewTokenPair::New();
    auto [v2, vh2] = scenic::ViewTokenPair::New();
    auto [root_session, root_resources] = CreateScene();

    scenic::Session* const session = root_session.session();
    scenic::Scene* const scene = &root_resources.scene;

    scenic::ViewHolder parent_view_holder(session, std::move(vh1), "1");
    scene->AddChild(parent_view_holder);
    RequestToPresent(session);

    SessionWrapper parent = CreateClient("parent_view", std::move(v1));
    scenic::ViewHolder child_view_holder(parent.session(), std::move(vh2), "2");
    parent.view()->AddChild(child_view_holder);
    RequestToPresent(parent.session());

    SessionWrapper child = CreateClient("child_view", std::move(v2));
    RequestToPresent(child.session());

    ViewRef parent_view_ref, child_view_ref;
    fidl::Clone(parent.view_ref(), &parent_view_ref);
    fidl::Clone(child.view_ref(), &child_view_ref);

    root_session_ = std::make_unique<SessionWrapper>(std::move(root_session));
    parent_ = std::make_unique<SessionWrapper>(std::move(parent));
    child_ = std::make_unique<SessionWrapper>(std::move(child));
    root_resources_ = std::make_unique<ResourceGraph>(std::move(root_resources));

    return {std::move(parent_view_ref), std::move(child_view_ref)};
  }

 protected:
  uint32_t test_display_width_px() const override { return 5; }
  uint32_t test_display_height_px() const override { return 5; }

  std::unique_ptr<ResourceGraph> root_resources_;
  std::unique_ptr<SessionWrapper> root_session_;
  std::unique_ptr<SessionWrapper> parent_;
  std::unique_ptr<SessionWrapper> child_;
};

scenic_impl::input::InjectorSettings StandardInjectorSettings() {
  return {.dispatch_policy = fuchsia::ui::pointerflow::DispatchPolicy::EXCLUSIVE,
          .device_id = 1,
          .device_type = fuchsia::ui::input3::PointerDeviceType::TOUCH,
          .context_koid = 1,
          .target_koid = 2};
}

fuchsia::ui::pointerflow::Event InjectionEventTemplate() {
  fuchsia::ui::pointerflow::Event event;
  event.set_timestamp(1111);
  event.set_pointer_id(2222);
  event.set_phase(fuchsia::ui::input3::PointerEventPhase::CHANGE);
  event.set_position_x(3333);
  event.set_position_y(4444);
  return event;
}

TEST_F(InputInjectionTest, RegisterAttemptWithCorrectArguments_ShouldSucceed) {
  auto [parent_view_ref, child_view_ref] = SetupSceneWithParentAndChildViews();

  fuchsia::ui::pointerflow::InjectorPtr injector;

  bool register_callback_fired = false;
  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t status) {
    error_callback_fired = true;
    FX_LOGS(INFO) << "Error: " << status;
  });
  {
    fuchsia::ui::pointerflow::InjectorConfig config;
    {
      fuchsia::ui::pointerflow::DeviceConfig device_config;
      device_config.set_device_id(1);
      device_config.set_device_type(fuchsia::ui::input3::PointerDeviceType::TOUCH);
      config.set_device_config(std::move(device_config));
    }
    {
      fuchsia::ui::pointerflow::InjectorContext context;
      context.set_view(std::move(parent_view_ref));
      config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerflow::InjectorTarget target;
      target.set_view(std::move(child_view_ref));
      config.set_target(std::move(target));
    }
    config.set_dispatch_policy(fuchsia::ui::pointerflow::DispatchPolicy::EXCLUSIVE);

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });
  }

  RunLoopUntilIdle();

  EXPECT_TRUE(register_callback_fired);
  EXPECT_FALSE(error_callback_fired);
}

TEST_F(InputInjectionTest, RegisterAttemptWithBadDeviceConfig_ShouldFail) {
  auto [parent_view_ref, child_view_ref] = SetupSceneWithParentAndChildViews();

  fuchsia::ui::pointerflow::InjectorPtr injector;

  fuchsia::ui::pointerflow::InjectorConfig base_config;
  {
    {
      fuchsia::ui::pointerflow::InjectorContext context;
      context.set_view(std::move(parent_view_ref));
      base_config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerflow::InjectorTarget target;
      target.set_view(std::move(child_view_ref));
      base_config.set_target(std::move(target));
    }
    base_config.set_dispatch_policy(fuchsia::ui::pointerflow::DispatchPolicy::EXCLUSIVE);
  }

  {  // No device config.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // No device id.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);
    fuchsia::ui::pointerflow::DeviceConfig device_config;
    device_config.set_device_type(fuchsia::ui::input3::PointerDeviceType::TOUCH);
    config.set_device_config(std::move(device_config));

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // No device type.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);
    fuchsia::ui::pointerflow::DeviceConfig device_config;
    device_config.set_device_id(1);
    config.set_device_config(std::move(device_config));

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // No device type.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);
    fuchsia::ui::pointerflow::DeviceConfig device_config;
    device_config.set_device_id(1);
    // Set to not TOUCH.
    device_config.set_device_type(static_cast<fuchsia::ui::input3::PointerDeviceType>(
        static_cast<uint32_t>(fuchsia::ui::input3::PointerDeviceType::TOUCH) + 1));
    config.set_device_config(std::move(device_config));

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }
}

TEST_F(InputInjectionTest, RegisterAttemptWithBadContextOrTarget_ShouldFail) {
  auto [parent_view_ref, child_view_ref] = SetupSceneWithParentAndChildViews();

  fuchsia::ui::pointerflow::InjectorPtr injector;

  fuchsia::ui::pointerflow::InjectorConfig base_config;
  {
    fuchsia::ui::pointerflow::DeviceConfig device_config;
    {
      device_config.set_device_id(1);
      device_config.set_device_type(fuchsia::ui::input3::PointerDeviceType::TOUCH);
      base_config.set_device_config(std::move(device_config));
    }
    base_config.set_dispatch_policy(fuchsia::ui::pointerflow::DispatchPolicy::EXCLUSIVE);
  }

  {  // No context.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);

    ViewRef child_clone;
    fidl::Clone(child_view_ref, &child_clone);

    fuchsia::ui::pointerflow::InjectorTarget target;
    target.set_view(std::move(child_clone));
    config.set_target(std::move(target));

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // No target.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);

    ViewRef parent_clone;
    fidl::Clone(parent_view_ref, &parent_clone);

    fuchsia::ui::pointerflow::InjectorContext context;
    context.set_view(std::move(parent_clone));
    config.set_context(std::move(context));

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // Context equals target.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);
    ViewRef parent_clone1, parent_clone2;
    fidl::Clone(parent_view_ref, &parent_clone1);
    fidl::Clone(parent_view_ref, &parent_clone2);
    {
      fuchsia::ui::pointerflow::InjectorContext context;
      context.set_view(std::move(parent_clone1));
      config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerflow::InjectorTarget target;
      target.set_view(std::move(parent_clone2));
      config.set_target(std::move(target));
    }

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // Context is descendant of target.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);
    ViewRef parent_clone, child_clone;
    fidl::Clone(parent_view_ref, &parent_clone);
    fidl::Clone(child_view_ref, &child_clone);

    // Swap context and target.
    {
      fuchsia::ui::pointerflow::InjectorContext context;
      context.set_view(std::move(child_clone));
      config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerflow::InjectorTarget target;
      target.set_view(std::move(parent_clone));
      config.set_target(std::move(target));
    }

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // Context is unregistered.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);
    ViewRef child_clone;
    fidl::Clone(child_view_ref, &child_clone);
    auto [control_ref, unregistered_view_ref] = scenic::ViewRefPair::New();
    {
      fuchsia::ui::pointerflow::InjectorContext context;
      context.set_view(std::move(unregistered_view_ref));
      config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerflow::InjectorTarget target;
      target.set_view(std::move(child_clone));
      config.set_target(std::move(target));
    }

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // Target is unregistered.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);
    ViewRef parent_clone;
    fidl::Clone(parent_view_ref, &parent_clone);
    auto [control_ref, unregistered_view_ref] = scenic::ViewRefPair::New();
    {
      fuchsia::ui::pointerflow::InjectorContext context;
      context.set_view(std::move(parent_clone));
      config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerflow::InjectorTarget target;
      target.set_view(std::move(unregistered_view_ref));
      config.set_target(std::move(target));
    }

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // Context is detached from scene.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);
    ViewRef parent_clone;
    fidl::Clone(parent_view_ref, &parent_clone);
    ViewRef child_clone;
    fidl::Clone(child_view_ref, &child_clone);
    {
      fuchsia::ui::pointerflow::InjectorContext context;
      context.set_view(std::move(parent_clone));
      config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerflow::InjectorTarget target;
      target.set_view(std::move(child_clone));
      config.set_target(std::move(target));
    }

    // Detach from scene.
    root_resources_->scene.DetachChildren();
    RequestToPresent(root_session_->session());

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }
}

TEST_F(InputInjectionTest, RegisterAttemptWithBadDispatchPolicy_ShouldFail) {
  auto [parent_view_ref, child_view_ref] = SetupSceneWithParentAndChildViews();

  fuchsia::ui::pointerflow::InjectorPtr injector;

  fuchsia::ui::pointerflow::InjectorConfig base_config;
  {
    fuchsia::ui::pointerflow::DeviceConfig device_config;
    device_config.set_device_id(1);
    device_config.set_device_type(fuchsia::ui::input3::PointerDeviceType::TOUCH);
    base_config.set_device_config(std::move(device_config));
  }
  {
    fuchsia::ui::pointerflow::InjectorContext context;
    context.set_view(std::move(parent_view_ref));
    base_config.set_context(std::move(context));
  }
  {
    fuchsia::ui::pointerflow::InjectorTarget target;
    target.set_view(std::move(child_view_ref));
    base_config.set_target(std::move(target));
  }

  {  // No dispatch policy.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // Unallowed dispatch policy.
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerflow::InjectorConfig config;
    fidl::Clone(base_config, &config);
    config.set_dispatch_policy(fuchsia::ui::pointerflow::DispatchPolicy::ALL_HIT_PARALLEL);

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }
}

TEST_F(InputInjectionTest, ChannelDying_ShouldNotCrash) {
  auto [parent_view_ref, child_view_ref] = SetupSceneWithParentAndChildViews();

  {
    fuchsia::ui::pointerflow::InjectorPtr injector;

    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });
    {
      fuchsia::ui::pointerflow::InjectorConfig config;
      {
        fuchsia::ui::pointerflow::DeviceConfig device_config;
        device_config.set_device_id(1);
        device_config.set_device_type(fuchsia::ui::input3::PointerDeviceType::TOUCH);
        config.set_device_config(std::move(device_config));
      }
      {
        fuchsia::ui::pointerflow::InjectorContext context;
        context.set_view(std::move(parent_view_ref));
        config.set_context(std::move(context));
      }
      {
        fuchsia::ui::pointerflow::InjectorTarget target;
        target.set_view(std::move(child_view_ref));
        config.set_target(std::move(target));
      }
      config.set_dispatch_policy(fuchsia::ui::pointerflow::DispatchPolicy::EXCLUSIVE);

      input_system()->Register(std::move(config), injector.NewRequest(),
                               [&register_callback_fired] { register_callback_fired = true; });
    }

    RunLoopUntilIdle();

    EXPECT_TRUE(register_callback_fired);
    EXPECT_FALSE(error_callback_fired);
  }  // |injector| goes out of scope.

  RunLoopUntilIdle();
}

TEST_F(InputInjectionTest, MultipleRegistrations_ShouldSucceed) {
  auto [parent_view_ref, child_view_ref] = SetupSceneWithParentAndChildViews();

  fuchsia::ui::pointerflow::InjectorConfig config;
  {
    {
      fuchsia::ui::pointerflow::DeviceConfig device_config;
      device_config.set_device_id(1);
      device_config.set_device_type(fuchsia::ui::input3::PointerDeviceType::TOUCH);
      config.set_device_config(std::move(device_config));
    }
    {
      fuchsia::ui::pointerflow::InjectorContext context;
      context.set_view(std::move(parent_view_ref));
      config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerflow::InjectorTarget target;
      target.set_view(std::move(child_view_ref));
      config.set_target(std::move(target));
    }
    config.set_dispatch_policy(fuchsia::ui::pointerflow::DispatchPolicy::EXCLUSIVE);
  }
  fuchsia::ui::pointerflow::InjectorConfig config2;
  fidl::Clone(config, &config2);

  fuchsia::ui::pointerflow::InjectorPtr injector;
  {
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });
    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });
    RunLoopUntilIdle();
    EXPECT_TRUE(register_callback_fired);
    EXPECT_FALSE(error_callback_fired);
  }

  fuchsia::ui::pointerflow::InjectorPtr injector2;
  {
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector2.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });
    input_system()->Register(std::move(config2), injector2.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });
    RunLoopUntilIdle();
    EXPECT_TRUE(register_callback_fired);
    EXPECT_FALSE(error_callback_fired);
  }
}

TEST(InjectorTest, InjectedEvents_ShouldTriggerTheInjectLambda) {
  async::TestLoop test_loop;
  async_set_default_dispatcher(test_loop.dispatcher());

  // Set up an isolated Injector.
  fuchsia::ui::pointerflow::InjectorPtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  bool connectivity_is_good = true;
  uint32_t num_injections = 0;
  scenic_impl::input::Injector injector_impl(
      /*id=*/1, StandardInjectorSettings(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [&connectivity_is_good](zx_koid_t, zx_koid_t) { return connectivity_is_good; },
      /*inject=*/[&num_injections](auto...) { ++num_injections; });

  {  // Inject one event.
    bool injection_callback_fired = false;
    fuchsia::ui::pointerflow::Event event = InjectionEventTemplate();
    event.set_phase(fuchsia::ui::input3::PointerEventPhase::ADD);
    std::vector<fuchsia::ui::pointerflow::Event> events;
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)},
                     [&injection_callback_fired] { injection_callback_fired = true; });
    test_loop.RunUntilIdle();
    EXPECT_TRUE(injection_callback_fired);
  }

  // 2 injections, since an injected ADD becomes "ADD; DOWN"
  // in fuchsia.ui.input.PointerEvent's state machine.
  EXPECT_EQ(num_injections, 2u);

  {  // Inject CHANGE event.
    bool injection_callback_fired = false;
    std::vector<fuchsia::ui::pointerflow::Event> events;
    fuchsia::ui::pointerflow::Event event1 = InjectionEventTemplate();
    event1.set_phase(fuchsia::ui::input3::PointerEventPhase::CHANGE);
    events.emplace_back(std::move(event1));
    injector->Inject({std::move(events)},
                     [&injection_callback_fired] { injection_callback_fired = true; });
    test_loop.RunUntilIdle();
    EXPECT_TRUE(injection_callback_fired);

    EXPECT_EQ(num_injections, 3u);
  }

  {  // Inject remove event.
    bool injection_callback_fired = false;
    std::vector<fuchsia::ui::pointerflow::Event> events;
    fuchsia::ui::pointerflow::Event event2 = InjectionEventTemplate();
    event2.set_phase(fuchsia::ui::input3::PointerEventPhase::REMOVE);
    events.emplace_back(std::move(event2));
    injector->Inject({std::move(events)},
                     [&injection_callback_fired] { injection_callback_fired = true; });
    test_loop.RunUntilIdle();
    EXPECT_TRUE(injection_callback_fired);
  }

  // 5 injections, since an injected REMOVE becomes "UP; REMOVE"
  // in fuchsia.ui.input.PointerEvent's state machine.
  EXPECT_EQ(num_injections, 5u);
  EXPECT_FALSE(error_callback_fired);
}

TEST(InjectorTest, InjectionWithNoEvent_ShouldCloseChannel) {
  // Test loop to be able to control dispatch without having to create an entire test class
  // subclassing TestLoopFixture.
  async::TestLoop test_loop;
  async_set_default_dispatcher(test_loop.dispatcher());

  // Set up an isolated Injector.
  fuchsia::ui::pointerflow::InjectorPtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  scenic_impl::input::Injector injector_impl(
      /*id=*/1, StandardInjectorSettings(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [](auto...) { return true; },
      /*inject=*/
      [](auto...) {});

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
  async_set_default_dispatcher(test_loop.dispatcher());

  // Set up an isolated Injector.
  fuchsia::ui::pointerflow::InjectorPtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  std::vector<uint32_t> cancelled_streams;
  scenic_impl::input::Injector injector_impl(
      /*id=*/1, StandardInjectorSettings(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [](auto...) { return true; },
      /*inject=*/
      [&cancelled_streams](zx_koid_t, zx_koid_t, const fuchsia::ui::input::PointerEvent& event) {
        if (event.phase == fuchsia::ui::input::PointerEventPhase::CANCEL)
          cancelled_streams.push_back(event.pointer_id);
      });

  // Start three streams and end one.
  {
    fuchsia::ui::pointerflow::Event event = InjectionEventTemplate();
    event.set_pointer_id(1);
    event.set_phase(Phase::ADD);
    std::vector<fuchsia::ui::pointerflow::Event> events;
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)}, [] {});
  }
  {
    fuchsia::ui::pointerflow::Event event = InjectionEventTemplate();
    event.set_pointer_id(2);
    event.set_phase(Phase::ADD);
    std::vector<fuchsia::ui::pointerflow::Event> events;
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)}, [] {});
  }
  {
    fuchsia::ui::pointerflow::Event event = InjectionEventTemplate();
    event.set_pointer_id(3);
    event.set_phase(Phase::ADD);
    std::vector<fuchsia::ui::pointerflow::Event> events;
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)}, [] {});
  }
  {
    fuchsia::ui::pointerflow::Event event = InjectionEventTemplate();
    event.set_pointer_id(1);
    event.set_phase(Phase::REMOVE);
    std::vector<fuchsia::ui::pointerflow::Event> events;
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
  async_set_default_dispatcher(test_loop.dispatcher());

  // Set up an isolated Injector.
  fuchsia::ui::pointerflow::InjectorPtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  std::vector<uint32_t> cancelled_streams;
  scenic_impl::input::Injector injector_impl(
      /*id=*/1, StandardInjectorSettings(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [](auto...) { return true; },
      /*inject=*/
      [&cancelled_streams](zx_koid_t, zx_koid_t, const fuchsia::ui::input::PointerEvent& event) {
        if (event.phase == fuchsia::ui::input::PointerEventPhase::CANCEL)
          cancelled_streams.push_back(event.pointer_id);
      });

  // Start three streams and end one.
  {
    std::vector<fuchsia::ui::pointerflow::Event> events;
    {
      fuchsia::ui::pointerflow::Event event = InjectionEventTemplate();
      event.set_pointer_id(1);
      event.set_phase(Phase::ADD);
      events.emplace_back(std::move(event));
    }
    {
      fuchsia::ui::pointerflow::Event event = InjectionEventTemplate();
      event.set_pointer_id(2);
      event.set_phase(Phase::ADD);
      events.emplace_back(std::move(event));
    }
    {
      fuchsia::ui::pointerflow::Event event = InjectionEventTemplate();
      event.set_pointer_id(3);
      event.set_phase(Phase::ADD);
      events.emplace_back(std::move(event));
    }
    {
      fuchsia::ui::pointerflow::Event event = InjectionEventTemplate();
      event.set_pointer_id(1);
      event.set_phase(Phase::REMOVE);
      events.emplace_back(std::move(event));
    }
    injector->Inject({std::move(events)}, [] {});
  }

  // Inject an event with missing fields to cause the channel to close.
  {
    std::vector<fuchsia::ui::pointerflow::Event> events;
    events.emplace_back();
    injector->Inject(std::move(events), [] {});
  }
  test_loop.RunUntilIdle();

  EXPECT_TRUE(error_callback_fired);
  // Should receive CANCEL events for the two ongoing streams; 2 and 3.
  EXPECT_THAT(cancelled_streams, testing::UnorderedElementsAre(2, 3));
}

// Test for lazy connectivity detection.
// TODO(50348): Remove when instant connectivity breakage detection is added.
TEST(InjectorTest, InjectionWithBadConnectivity_ShouldCloseChannel) {
  // Test loop to be able to control dispatch without having to create an entire test class
  // subclassing TestLoopFixture.
  async::TestLoop test_loop;
  async_set_default_dispatcher(test_loop.dispatcher());

  // Set up an isolated Injector.
  fuchsia::ui::pointerflow::InjectorPtr injector;

  bool error_callback_fired = false;
  zx_status_t error = ZX_OK;
  injector.set_error_handler([&error_callback_fired, &error](zx_status_t status) {
    error_callback_fired = true;
    error = status;
  });

  bool connectivity_is_good = true;
  uint32_t num_cancel_events = 0;
  scenic_impl::input::Injector injector_impl(
      /*id=*/1, StandardInjectorSettings(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [&connectivity_is_good](zx_koid_t, zx_koid_t) { return connectivity_is_good; },
      /*inject=*/
      [&num_cancel_events](zx_koid_t, zx_koid_t, const fuchsia::ui::input::PointerEvent& event) {
        num_cancel_events += event.phase == fuchsia::ui::input::PointerEventPhase::CANCEL ? 1 : 0;
      });

  // Start event stream while connectivity is good.
  {
    fuchsia::ui::pointerflow::Event event = InjectionEventTemplate();
    event.set_phase(Phase::ADD);
    event.set_pointer_id(1);
    std::vector<fuchsia::ui::pointerflow::Event> events;
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
    fuchsia::ui::pointerflow::Event event = InjectionEventTemplate();
    event.set_phase(Phase::CHANGE);
    std::vector<fuchsia::ui::pointerflow::Event> events;
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
                         testing::Range(0, 5));

TEST_P(InjectorInvalidEventsTest, InjectEventWithMissingField_ShouldCloseChannel) {
  // Create event with a missing field based on GetParam().
  fuchsia::ui::pointerflow::Event event = InjectionEventTemplate();
  switch (GetParam()) {
    case 0:
      event.clear_timestamp();
      break;
    case 1:
      event.clear_pointer_id();
      break;
    case 2:
      event.clear_phase();
      break;
    case 3:
      event.clear_position_x();
      break;
    case 4:
      event.clear_position_y();
      break;
  }

  // Set up an isolated Injector.
  fuchsia::ui::pointerflow::InjectorPtr injector;

  bool error_callback_fired = false;
  zx_status_t error = ZX_OK;
  injector.set_error_handler([&error_callback_fired, &error](zx_status_t status) {
    error_callback_fired = true;
    error = status;
  });

  scenic_impl::input::Injector injector_impl(
      /*id=*/1, StandardInjectorSettings(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [](auto...) { return true; },
      /*inject=*/
      [](auto...) {});

  bool injection_callback_fired = false;
  std::vector<fuchsia::ui::pointerflow::Event> events;
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
  fuchsia::ui::pointerflow::InjectorPtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  scenic_impl::input::Injector injector_impl(
      /*id=*/1, StandardInjectorSettings(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [](auto...) { return true; },  // Always true.
      /*inject=*/
      [](auto...) {});

  std::vector<fuchsia::ui::pointerflow::Event> events;
  for (auto [pointer_id, phase] : GetParam()) {
    fuchsia::ui::pointerflow::Event event = InjectionEventTemplate();
    event.set_pointer_id(pointer_id);
    event.set_phase(phase);
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
  fuchsia::ui::pointerflow::InjectorPtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  scenic_impl::input::Injector injector_impl(
      /*id=*/1, StandardInjectorSettings(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [](auto...) { return true; },  // Always true.
      /*inject=*/
      [](auto...) {});

  for (auto [pointer_id, phase] : GetParam()) {
    bool injection_callback_fired = false;
    fuchsia::ui::pointerflow::Event event = InjectionEventTemplate();
    event.set_pointer_id(pointer_id);
    event.set_phase(phase);
    std::vector<fuchsia::ui::pointerflow::Event> events;
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
  fuchsia::ui::pointerflow::InjectorPtr injector;

  bool error_callback_fired = false;
  zx_status_t error = ZX_OK;
  injector.set_error_handler([&error_callback_fired, &error](zx_status_t status) {
    error_callback_fired = true;
    error = status;
  });

  scenic_impl::input::Injector injector_impl(
      /*id=*/1, StandardInjectorSettings(), injector.NewRequest(),
      /*is_descendant_and_connected=*/[](auto...) { return true; },
      /*inject=*/[](auto...) {});

  fuchsia::ui::pointerflow::Event event = InjectionEventTemplate();

  // Run event stream.
  std::vector<fuchsia::ui::pointerflow::Event> events;
  for (auto [pointer_id, phase] : GetParam()) {
    fuchsia::ui::pointerflow::Event event = InjectionEventTemplate();
    event.set_pointer_id(pointer_id);
    event.set_phase(phase);
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
  fuchsia::ui::pointerflow::InjectorPtr injector;

  bool error_callback_fired = false;
  zx_status_t error = ZX_OK;
  injector.set_error_handler([&error_callback_fired, &error](zx_status_t status) {
    error_callback_fired = true;
    error = status;
  });

  scenic_impl::input::Injector injector_impl(
      /*id=*/1, StandardInjectorSettings(), injector.NewRequest(),
      /*is_descendant_and_connected=*/[](auto...) { return true; },
      /*inject=*/[](auto...) {});

  // Run event stream.
  for (auto [pointer_id, phase] : GetParam()) {
    fuchsia::ui::pointerflow::Event event = InjectionEventTemplate();
    event.set_pointer_id(pointer_id);
    event.set_phase(phase);
    std::vector<fuchsia::ui::pointerflow::Event> events;
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)}, [] {});
    RunLoopUntilIdle();
  }

  EXPECT_TRUE(error_callback_fired);
  EXPECT_EQ(error, ZX_ERR_BAD_STATE);
}

}  // namespace
}  // namespace lib_ui_input_tests
