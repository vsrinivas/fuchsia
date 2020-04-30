// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/input/input_system.h"
#include "src/ui/scenic/lib/input/tests/util.h"

using fuchsia::ui::views::ViewRef;

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

  scenic_impl::input::InjectorSettings settings{
      .dispatch_policy = fuchsia::ui::pointerflow::DispatchPolicy::EXCLUSIVE,
      .device_id = 1,
      .device_type = fuchsia::ui::input3::PointerDeviceType::TOUCH,
      .context_koid = 1,
      .target_koid = 2};

  bool connectivity_is_good = true;
  scenic_impl::input::Injector injector_impl(
      /*id=*/1, settings, injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [&connectivity_is_good](zx_koid_t, zx_koid_t) { return connectivity_is_good; });

  fuchsia::ui::pointerflow::Event event;
  {
    event.set_timestamp(0);
    event.set_pointer_id(1);
    event.set_phase(fuchsia::ui::input3::PointerEventPhase::ADD);
    event.set_position_x(3);
    event.set_position_y(4);
  }

  // Inject while connectivity is good.
  bool injection_callback_fired1 = false;
  {
    fuchsia::ui::pointerflow::Event clone;
    fidl::Clone(event, &clone);
    std::vector<fuchsia::ui::pointerflow::Event> events;
    events.emplace_back(std::move(clone));
    injector->Inject({std::move(events)},
                     [&injection_callback_fired1] { injection_callback_fired1 = true; });
    test_loop.RunUntilIdle();
  }

  // Connectivity was good. No problems.
  EXPECT_TRUE(injection_callback_fired1);
  EXPECT_FALSE(error_callback_fired);

  // Inject with bad connectivity.
  connectivity_is_good = false;
  bool injection_callback_fired2 = false;
  {
    fuchsia::ui::pointerflow::Event clone;
    fidl::Clone(event, &clone);
    std::vector<fuchsia::ui::pointerflow::Event> events;
    events.emplace_back(std::move(clone));
    injector->Inject({std::move(events)},
                     [&injection_callback_fired2] { injection_callback_fired2 = true; });
    test_loop.RunUntilIdle();
  }

  // Connectivity was bad, so channel should be closed.
  EXPECT_FALSE(injection_callback_fired2);
  EXPECT_TRUE(error_callback_fired);
  EXPECT_EQ(error, ZX_ERR_BAD_STATE);
}

}  // namespace
}  // namespace lib_ui_input_tests
