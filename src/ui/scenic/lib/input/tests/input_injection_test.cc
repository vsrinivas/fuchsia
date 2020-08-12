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

#include "src/ui/scenic/lib/input/helper.h"
#include "src/ui/scenic/lib/input/input_system.h"
#include "src/ui/scenic/lib/input/tests/util.h"

using fuchsia::ui::views::ViewRef;
using Phase = fuchsia::ui::pointerinjector::EventPhase;
using DeviceType = fuchsia::ui::pointerinjector::DeviceType;

namespace lib_ui_input_tests {
namespace {

// clang-format off
static constexpr std::array<float, 9> kIdentityMatrix = {
  1, 0, 0, // first column
  0, 1, 0, // second column
  0, 0, 1, // third column
};
// clang-format on

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

    ViewRef parent_view_ref = parent.view_ref();
    ViewRef child_view_ref = child.view_ref();

    root_session_ = std::make_unique<SessionWrapper>(std::move(root_session));
    parent_ = std::make_unique<SessionWrapper>(std::move(parent));
    child_ = std::make_unique<SessionWrapper>(std::move(child));
    root_resources_ = std::make_unique<ResourceGraph>(std::move(root_resources));

    return {std::move(parent_view_ref), std::move(child_view_ref)};
  }

 protected:
  fuchsia::ui::pointerinjector::Config ConfigTemplate(
      const fuchsia::ui::views::ViewRef& context_view_ref,
      const fuchsia::ui::views::ViewRef& target_view_ref) {
    fuchsia::ui::pointerinjector::Config config;
    config.set_device_id(1);
    config.set_device_type(DeviceType::TOUCH);
    config.set_dispatch_policy(fuchsia::ui::pointerinjector::DispatchPolicy::EXCLUSIVE_TARGET);
    {
      fuchsia::ui::pointerinjector::Viewport viewport;
      viewport.set_extents(FullScreenExtents());
      viewport.set_viewport_to_context_transform(kIdentityMatrix);
      config.set_viewport(std::move(viewport));
    }
    {
      fuchsia::ui::pointerinjector::Context context;
      ViewRef context_clone;
      fidl::Clone(context_view_ref, &context_clone);
      context.set_view(std::move(context_clone));
      config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerinjector::Target target;
      ViewRef target_clone;
      fidl::Clone(target_view_ref, &target_clone);
      target.set_view(std::move(target_clone));
      config.set_target(std::move(target));
    }
    return config;
  }

  uint32_t test_display_width_px() const override { return 5; }
  uint32_t test_display_height_px() const override { return 5; }
  std::array<std::array<float, 2>, 2> FullScreenExtents() const {
    return {{{0, 0},
             {static_cast<float>(test_display_width_px()),
              static_cast<float>(test_display_height_px())}}};
  }

  std::unique_ptr<ResourceGraph> root_resources_;
  std::unique_ptr<SessionWrapper> root_session_;
  std::unique_ptr<SessionWrapper> parent_;
  std::unique_ptr<SessionWrapper> child_;
};

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
      .context_from_viewport_transform =
          scenic_impl::input::ColumnMajorMat3VectorToMat4(kIdentityMatrix),
  };
}

fuchsia::ui::pointerinjector::Event InjectionEventTemplate() {
  fuchsia::ui::pointerinjector::Event event;
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

TEST_F(InputInjectionTest, RegisterAttemptWithCorrectArguments_ShouldSucceed) {
  const auto [parent_view_ref, child_view_ref] = SetupSceneWithParentAndChildViews();

  fuchsia::ui::pointerinjector::DevicePtr injector;
  bool register_callback_fired = false;
  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t status) {
    error_callback_fired = true;
    FX_LOGS(INFO) << "Error: " << status;
  });
  {
    fuchsia::ui::pointerinjector::Config config = ConfigTemplate(parent_view_ref, child_view_ref);
    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });
  }

  RunLoopUntilIdle();

  EXPECT_TRUE(register_callback_fired);
  EXPECT_FALSE(error_callback_fired);
}

TEST_F(InputInjectionTest, RegisterAttemptWithBadDeviceConfig_ShouldFail) {
  const auto [parent_view_ref, child_view_ref] = SetupSceneWithParentAndChildViews();
  const fuchsia::ui::pointerinjector::Config base_config =
      ConfigTemplate(std::move(parent_view_ref), std::move(child_view_ref));

  {  // No device id.
    fuchsia::ui::pointerinjector::DevicePtr injector;
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerinjector::Config config;
    fidl::Clone(base_config, &config);
    config.clear_device_id();

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // No device type.
    fuchsia::ui::pointerinjector::DevicePtr injector;
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerinjector::Config config;
    fidl::Clone(base_config, &config);
    config.clear_device_type();

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // Wrong device type.
    fuchsia::ui::pointerinjector::DevicePtr injector;
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerinjector::Config config;
    fidl::Clone(base_config, &config);
    // Set to not TOUCH.
    config.set_device_type(static_cast<DeviceType>(static_cast<uint32_t>(12421)));

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }
}

TEST_F(InputInjectionTest, RegisterAttemptWithBadContextOrTarget_ShouldFail) {
  const auto [parent_view_ref, child_view_ref] = SetupSceneWithParentAndChildViews();
  const fuchsia::ui::pointerinjector::Config base_config =
      ConfigTemplate(parent_view_ref, child_view_ref);

  {  // No context.
    fuchsia::ui::pointerinjector::DevicePtr injector;
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerinjector::Config config;
    fidl::Clone(base_config, &config);
    config.clear_context();

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // No target.
    fuchsia::ui::pointerinjector::DevicePtr injector;
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerinjector::Config config;
    fidl::Clone(base_config, &config);
    config.clear_target();

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // Context equals target.
    fuchsia::ui::pointerinjector::DevicePtr injector;
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerinjector::Config config;
    fidl::Clone(base_config, &config);
    ViewRef parent_clone1, parent_clone2;
    fidl::Clone(parent_view_ref, &parent_clone1);
    fidl::Clone(parent_view_ref, &parent_clone2);
    {
      fuchsia::ui::pointerinjector::Context context;
      context.set_view(std::move(parent_clone1));
      config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerinjector::Target target;
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
    fuchsia::ui::pointerinjector::DevicePtr injector;
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerinjector::Config config;
    fidl::Clone(base_config, &config);
    ViewRef parent_clone, child_clone;
    fidl::Clone(parent_view_ref, &parent_clone);
    fidl::Clone(child_view_ref, &child_clone);

    // Swap context and target.
    {
      fuchsia::ui::pointerinjector::Context context;
      context.set_view(std::move(child_clone));
      config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerinjector::Target target;
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
    fuchsia::ui::pointerinjector::DevicePtr injector;
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerinjector::Config config;
    fidl::Clone(base_config, &config);
    ViewRef child_clone;
    fidl::Clone(child_view_ref, &child_clone);
    auto [control_ref, unregistered_view_ref] = scenic::ViewRefPair::New();
    {
      fuchsia::ui::pointerinjector::Context context;
      context.set_view(std::move(unregistered_view_ref));
      config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerinjector::Target target;
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
    fuchsia::ui::pointerinjector::DevicePtr injector;
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerinjector::Config config;
    fidl::Clone(base_config, &config);
    ViewRef parent_clone;
    fidl::Clone(parent_view_ref, &parent_clone);
    auto [control_ref, unregistered_view_ref] = scenic::ViewRefPair::New();
    {
      fuchsia::ui::pointerinjector::Context context;
      context.set_view(std::move(parent_clone));
      config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerinjector::Target target;
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
    fuchsia::ui::pointerinjector::DevicePtr injector;
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerinjector::Config config;
    fidl::Clone(base_config, &config);
    ViewRef parent_clone;
    fidl::Clone(parent_view_ref, &parent_clone);
    ViewRef child_clone;
    fidl::Clone(child_view_ref, &child_clone);
    {
      fuchsia::ui::pointerinjector::Context context;
      context.set_view(std::move(parent_clone));
      config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerinjector::Target target;
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
  const auto [parent_view_ref, child_view_ref] = SetupSceneWithParentAndChildViews();
  const fuchsia::ui::pointerinjector::Config base_config =
      ConfigTemplate(parent_view_ref, child_view_ref);

  {  // No dispatch policy.
    fuchsia::ui::pointerinjector::DevicePtr injector;
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerinjector::Config config;
    fidl::Clone(base_config, &config);
    config.clear_dispatch_policy();

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }

  {  // Unsupported dispatch policy.
    fuchsia::ui::pointerinjector::DevicePtr injector;
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerinjector::Config config;
    fidl::Clone(base_config, &config);
    config.set_dispatch_policy(static_cast<fuchsia::ui::pointerinjector::DispatchPolicy>(6323));

    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }
}

TEST_F(InputInjectionTest, ChannelDying_ShouldNotCrash) {
  const auto [parent_view_ref, child_view_ref] = SetupSceneWithParentAndChildViews();

  {
    fuchsia::ui::pointerinjector::DevicePtr injector;
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerinjector::Config config = ConfigTemplate(parent_view_ref, child_view_ref);
    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_TRUE(register_callback_fired);
    EXPECT_FALSE(error_callback_fired);
  }  // |injector| goes out of scope.

  RunLoopUntilIdle();
}

TEST_F(InputInjectionTest, MultipleRegistrations_ShouldSucceed) {
  const auto [parent_view_ref, child_view_ref] = SetupSceneWithParentAndChildViews();

  fuchsia::ui::pointerinjector::DevicePtr injector;
  {
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });
    fuchsia::ui::pointerinjector::Config config = ConfigTemplate(parent_view_ref, child_view_ref);
    input_system()->Register(std::move(config), injector.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });
    RunLoopUntilIdle();
    EXPECT_TRUE(register_callback_fired);
    EXPECT_FALSE(error_callback_fired);
  }

  fuchsia::ui::pointerinjector::DevicePtr injector2;
  {
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector2.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerinjector::Config config = ConfigTemplate(parent_view_ref, child_view_ref);
    input_system()->Register(std::move(config), injector2.NewRequest(),
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
  fuchsia::ui::pointerinjector::DevicePtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  bool connectivity_is_good = true;
  uint32_t num_injections = 0;
  scenic_impl::input::Injector injector_impl(
      InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [&connectivity_is_good](zx_koid_t, zx_koid_t) { return connectivity_is_good; },
      /*inject=*/[&num_injections](auto...) { ++num_injections; });

  {  // Inject one event.
    bool injection_callback_fired = false;
    fuchsia::ui::pointerinjector::Event event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_phase(Phase::ADD);
    std::vector<fuchsia::ui::pointerinjector::Event> events;
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
    std::vector<fuchsia::ui::pointerinjector::Event> events;
    fuchsia::ui::pointerinjector::Event event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_phase(Phase::CHANGE);
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)},
                     [&injection_callback_fired] { injection_callback_fired = true; });
    test_loop.RunUntilIdle();
    EXPECT_TRUE(injection_callback_fired);

    EXPECT_EQ(num_injections, 3u);
  }

  {  // Inject remove event.
    bool injection_callback_fired = false;
    std::vector<fuchsia::ui::pointerinjector::Event> events;
    fuchsia::ui::pointerinjector::Event event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_phase(Phase::REMOVE);
    events.emplace_back(std::move(event));
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
  fuchsia::ui::pointerinjector::DevicePtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  scenic_impl::input::Injector injector_impl(
      InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
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
  fuchsia::ui::pointerinjector::DevicePtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  std::vector<uint32_t> cancelled_streams;
  scenic_impl::input::Injector injector_impl(
      InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [](auto...) { return true; },
      /*inject=*/
      [&cancelled_streams](const scenic_impl::input::InternalPointerEvent& event) {
        if (event.phase == scenic_impl::input::Phase::CANCEL)
          cancelled_streams.push_back(event.pointer_id);
      });

  // Start three streams and end one.
  {
    fuchsia::ui::pointerinjector::Event event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_pointer_id(1);
    event.mutable_data()->pointer_sample().set_phase(Phase::ADD);
    std::vector<fuchsia::ui::pointerinjector::Event> events;
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)}, [] {});
  }
  {
    fuchsia::ui::pointerinjector::Event event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_pointer_id(2);
    event.mutable_data()->pointer_sample().set_phase(Phase::ADD);
    std::vector<fuchsia::ui::pointerinjector::Event> events;
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)}, [] {});
  }
  {
    fuchsia::ui::pointerinjector::Event event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_pointer_id(3);
    event.mutable_data()->pointer_sample().set_phase(Phase::ADD);
    std::vector<fuchsia::ui::pointerinjector::Event> events;
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)}, [] {});
  }
  {
    fuchsia::ui::pointerinjector::Event event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_pointer_id(1);
    event.mutable_data()->pointer_sample().set_phase(Phase::REMOVE);
    std::vector<fuchsia::ui::pointerinjector::Event> events;
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
  fuchsia::ui::pointerinjector::DevicePtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  std::vector<uint32_t> cancelled_streams;
  scenic_impl::input::Injector injector_impl(
      InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [](auto...) { return true; },
      /*inject=*/
      [&cancelled_streams](const scenic_impl::input::InternalPointerEvent& event) {
        if (event.phase == scenic_impl::input::Phase::CANCEL)
          cancelled_streams.push_back(event.pointer_id);
      });

  // Start three streams and end one.
  {
    std::vector<fuchsia::ui::pointerinjector::Event> events;
    {
      fuchsia::ui::pointerinjector::Event event = InjectionEventTemplate();
      event.mutable_data()->pointer_sample().set_pointer_id(1);
      event.mutable_data()->pointer_sample().set_phase(Phase::ADD);
      events.emplace_back(std::move(event));
    }
    {
      fuchsia::ui::pointerinjector::Event event = InjectionEventTemplate();
      event.mutable_data()->pointer_sample().set_pointer_id(2);
      event.mutable_data()->pointer_sample().set_phase(Phase::ADD);
      events.emplace_back(std::move(event));
    }
    {
      fuchsia::ui::pointerinjector::Event event = InjectionEventTemplate();
      event.mutable_data()->pointer_sample().set_pointer_id(3);
      event.mutable_data()->pointer_sample().set_phase(Phase::ADD);
      events.emplace_back(std::move(event));
    }
    {
      fuchsia::ui::pointerinjector::Event event = InjectionEventTemplate();
      event.mutable_data()->pointer_sample().set_pointer_id(1);
      event.mutable_data()->pointer_sample().set_phase(Phase::REMOVE);
      events.emplace_back(std::move(event));
    }
    injector->Inject({std::move(events)}, [] {});
  }

  // Inject an event with missing fields to cause the channel to close.
  {
    std::vector<fuchsia::ui::pointerinjector::Event> events;
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
  async_set_default_dispatcher(test_loop.dispatcher());

  // Set up an isolated Injector.
  fuchsia::ui::pointerinjector::DevicePtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](auto) { error_callback_fired = true; });

  bool injection_lambda_fired = false;
  scenic_impl::input::Injector injector_impl(
      InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [](zx_koid_t, zx_koid_t) { return true; },
      /*inject=*/
      [&injection_lambda_fired](auto...) { injection_lambda_fired = true; });

  bool injection_callback_fired = false;
  fuchsia::ui::pointerinjector::Event event;
  std::vector<fuchsia::ui::pointerinjector::Event> events;
  events.emplace_back(std::move(event));
  injector->Inject({std::move(events)},
                   [&injection_callback_fired] { injection_callback_fired = true; });
  test_loop.RunUntilIdle();

  EXPECT_FALSE(injection_lambda_fired);
  EXPECT_FALSE(injection_callback_fired);
  EXPECT_TRUE(error_callback_fired);
}

// Test for lazy connectivity detection.
// TODO(fxbug.dev/50348): Remove when instant connectivity breakage detection is added.
TEST(InjectorTest, InjectionWithBadConnectivity_ShouldCloseChannel) {
  // Test loop to be able to control dispatch without having to create an entire test class
  // subclassing TestLoopFixture.
  async::TestLoop test_loop;
  async_set_default_dispatcher(test_loop.dispatcher());

  // Set up an isolated Injector.
  fuchsia::ui::pointerinjector::DevicePtr injector;

  bool error_callback_fired = false;
  zx_status_t error = ZX_OK;
  injector.set_error_handler([&error_callback_fired, &error](zx_status_t status) {
    error_callback_fired = true;
    error = status;
  });

  bool connectivity_is_good = true;
  uint32_t num_cancel_events = 0;
  scenic_impl::input::Injector injector_impl(
      InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [&connectivity_is_good](zx_koid_t, zx_koid_t) { return connectivity_is_good; },
      /*inject=*/
      [&num_cancel_events](const scenic_impl::input::InternalPointerEvent& event) {
        num_cancel_events += event.phase == scenic_impl::input::Phase::CANCEL ? 1 : 0;
      });

  // Start event stream while connectivity is good.
  {
    fuchsia::ui::pointerinjector::Event event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_phase(Phase::ADD);
    event.mutable_data()->pointer_sample().set_pointer_id(1);
    std::vector<fuchsia::ui::pointerinjector::Event> events;
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
    fuchsia::ui::pointerinjector::Event event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_phase(Phase::CHANGE);
    std::vector<fuchsia::ui::pointerinjector::Event> events;
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
  fuchsia::ui::pointerinjector::Event event = InjectionEventTemplate();
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
  fuchsia::ui::pointerinjector::DevicePtr injector;

  bool error_callback_fired = false;
  zx_status_t error = ZX_OK;
  injector.set_error_handler([&error_callback_fired, &error](zx_status_t status) {
    error_callback_fired = true;
    error = status;
  });

  scenic_impl::input::Injector injector_impl(
      InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [](auto...) { return true; },
      /*inject=*/
      [](auto...) {});

  bool injection_callback_fired = false;
  std::vector<fuchsia::ui::pointerinjector::Event> events;
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
  fuchsia::ui::pointerinjector::DevicePtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  scenic_impl::input::Injector injector_impl(
      InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [](auto...) { return true; },  // Always true.
      /*inject=*/
      [](auto...) {});

  std::vector<fuchsia::ui::pointerinjector::Event> events;
  for (auto [pointer_id, phase] : GetParam()) {
    fuchsia::ui::pointerinjector::Event event = InjectionEventTemplate();
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
  fuchsia::ui::pointerinjector::DevicePtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  scenic_impl::input::Injector injector_impl(
      InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/
      [](auto...) { return true; },  // Always true.
      /*inject=*/
      [](auto...) {});

  for (auto [pointer_id, phase] : GetParam()) {
    bool injection_callback_fired = false;
    fuchsia::ui::pointerinjector::Event event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_pointer_id(pointer_id);
    event.mutable_data()->pointer_sample().set_phase(phase);
    std::vector<fuchsia::ui::pointerinjector::Event> events;
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
  fuchsia::ui::pointerinjector::DevicePtr injector;

  bool error_callback_fired = false;
  zx_status_t error = ZX_OK;
  injector.set_error_handler([&error_callback_fired, &error](zx_status_t status) {
    error_callback_fired = true;
    error = status;
  });

  scenic_impl::input::Injector injector_impl(
      InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/[](auto...) { return true; },
      /*inject=*/[](auto...) {});

  fuchsia::ui::pointerinjector::Event event = InjectionEventTemplate();

  // Run event stream.
  std::vector<fuchsia::ui::pointerinjector::Event> events;
  for (auto [pointer_id, phase] : GetParam()) {
    fuchsia::ui::pointerinjector::Event event = InjectionEventTemplate();
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
  fuchsia::ui::pointerinjector::DevicePtr injector;

  bool error_callback_fired = false;
  zx_status_t error = ZX_OK;
  injector.set_error_handler([&error_callback_fired, &error](zx_status_t status) {
    error_callback_fired = true;
    error = status;
  });

  scenic_impl::input::Injector injector_impl(
      InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/[](auto...) { return true; },
      /*inject=*/[](auto...) {});

  // Run event stream.
  for (auto [pointer_id, phase] : GetParam()) {
    fuchsia::ui::pointerinjector::Event event = InjectionEventTemplate();
    event.mutable_data()->pointer_sample().set_pointer_id(pointer_id);
    event.mutable_data()->pointer_sample().set_phase(phase);
    std::vector<fuchsia::ui::pointerinjector::Event> events;
    events.emplace_back(std::move(event));
    injector->Inject({std::move(events)}, [] {});
    RunLoopUntilIdle();
  }

  EXPECT_TRUE(error_callback_fired);
  EXPECT_EQ(error, ZX_ERR_BAD_STATE);
}

TEST(InjectorTest, InjectedViewport_ShouldNotTriggerInjectLambda) {
  async::TestLoop test_loop;
  async_set_default_dispatcher(test_loop.dispatcher());

  // Set up an isolated Injector.
  fuchsia::ui::pointerinjector::DevicePtr injector;

  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  bool inject_lambda_fired = false;
  scenic_impl::input::Injector injector_impl(
      InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/[](zx_koid_t, zx_koid_t) { return true; },
      /*inject=*/[&inject_lambda_fired](auto...) { inject_lambda_fired = true; });

  {
    bool injection_callback_fired = false;
    fuchsia::ui::pointerinjector::Event event;
    event.set_timestamp(1);
    {
      fuchsia::ui::pointerinjector::Viewport viewport;
      viewport.set_extents({{{-242, -383}, {124, 252}}});
      viewport.set_viewport_to_context_transform(kIdentityMatrix);
      fuchsia::ui::pointerinjector::Data data;
      data.set_viewport(std::move(viewport));
      event.set_data(std::move(data));
    }

    std::vector<fuchsia::ui::pointerinjector::Event> events;
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

class ParameterizedInputInjectionTest : public InputInjectionTest,
                                        public testing::WithParamInterface<ViewportPair> {};

INSTANTIATE_TEST_SUITE_P(RegisterAttemptWithBadViewport_ShouldFail, ParameterizedInputInjectionTest,
                         testing::ValuesIn(BadViewportTestData()));

TEST_P(ParameterizedInputInjectionTest, RegisterAttemptWithBadViewport_ShouldFail) {
  const auto [parent_view_ref, child_view_ref] = SetupSceneWithParentAndChildViews();

  fuchsia::ui::pointerinjector::DevicePtr injector;
  bool register_callback_fired = false;
  bool error_callback_fired = false;
  injector.set_error_handler(
      [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

  fuchsia::ui::pointerinjector::Config config =
      ConfigTemplate(std::move(parent_view_ref), std::move(child_view_ref));
  {
    ViewportPair params = GetParam();
    fuchsia::ui::pointerinjector::Viewport viewport;
    if (params.first)
      viewport.set_extents(params.first.value());
    if (params.second)
      viewport.set_viewport_to_context_transform(params.second.value());
    config.set_viewport(std::move(viewport));
  }

  input_system()->Register(std::move(config), injector.NewRequest(),
                           [&register_callback_fired] { register_callback_fired = true; });

  RunLoopUntilIdle();

  EXPECT_FALSE(register_callback_fired);
  EXPECT_TRUE(error_callback_fired);
}

INSTANTIATE_TEST_SUITE_P(InjectBadViewport_ShouldCloseChannel, InjectorBadViewportTest,
                         testing::ValuesIn(BadViewportTestData()));

TEST_P(InjectorBadViewportTest, InjectBadViewport_ShouldCloseChannel) {
  async::TestLoop test_loop;
  async_set_default_dispatcher(test_loop.dispatcher());

  fuchsia::ui::pointerinjector::DevicePtr injector;
  bool error_callback_fired = false;
  injector.set_error_handler([&error_callback_fired](zx_status_t) { error_callback_fired = true; });

  bool inject_lambda_fired = false;
  scenic_impl::input::Injector injector_impl(
      InjectorSettingsTemplate(), ViewportTemplate(), injector.NewRequest(),
      /*is_descendant_and_connected=*/[](zx_koid_t, zx_koid_t) { return true; },
      /*inject=*/[&inject_lambda_fired](auto...) { inject_lambda_fired = true; });

  fuchsia::ui::pointerinjector::Event event;
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

  std::vector<fuchsia::ui::pointerinjector::Event> events;
  events.emplace_back(std::move(event));
  bool injection_callback_fired = false;
  injector->Inject({std::move(events)},
                   [&injection_callback_fired] { injection_callback_fired = true; });

  test_loop.RunUntilIdle();
  EXPECT_FALSE(injection_callback_fired);
  EXPECT_TRUE(error_callback_fired);
}

}  // namespace
}  // namespace lib_ui_input_tests
