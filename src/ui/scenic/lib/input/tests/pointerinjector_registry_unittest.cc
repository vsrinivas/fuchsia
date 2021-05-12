// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/pointerinjector_registry.h"

#include <lib/ui/scenic/cpp/view_ref_pair.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/scenic/lib/utils/helpers.h"
#include "src/ui/scenic/lib/utils/math.h"

using fuchsia::ui::views::ViewRef;
using DeviceType = fuchsia::ui::pointerinjector::DeviceType;

// Unit tests for the PointerinjectorRegistry class.

namespace input::test {

namespace {

// clang-format off
static constexpr std::array<float, 9> kIdentityMatrix = {
  1, 0, 0, // first column
  0, 1, 0, // second column
  0, 0, 1, // third column
};
// clang-format on

}  // namespace

class PointerinjectorRegistryTest : public gtest::TestLoopFixture {
 public:
  PointerinjectorRegistryTest()
      : registry_(/*context*/ nullptr, /*inject_touch_exclusive*/ [](auto...) {},
                  /*inject_touch_hit_tested*/ [](auto...) {}, inspect::Node()) {}

 protected:
  struct ScenePair {
    scenic::ViewRefPair parent;
    scenic::ViewRefPair child;

    ScenePair() : parent(scenic::ViewRefPair::New()), child(scenic::ViewRefPair::New()) {}
  };

  fuchsia::ui::pointerinjector::Config ConfigTemplate(const ViewRef& context_view_ref,
                                                      const ViewRef& target_view_ref) {
    fuchsia::ui::pointerinjector::Config config;
    config.set_device_id(1);
    config.set_device_type(DeviceType::TOUCH);
    config.set_dispatch_policy(fuchsia::ui::pointerinjector::DispatchPolicy::EXCLUSIVE_TARGET);
    {
      fuchsia::ui::pointerinjector::Viewport viewport;
      viewport.set_extents({{{0, 0}, {10, 10}}});
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

  ScenePair SetupSceneWithParentAndChildViews() {
    ScenePair scene_pair;
    const zx_koid_t parent_koid = utils::ExtractKoid(scene_pair.parent.view_ref);
    const zx_koid_t child_koid = utils::ExtractKoid(scene_pair.child.view_ref);

    auto snapshot = std::make_shared<view_tree::Snapshot>();
    snapshot->root = parent_koid;
    snapshot->view_tree[parent_koid] = {.children = {child_koid}};
    snapshot->view_tree[child_koid] = {.parent = parent_koid};
    registry_.OnNewViewTreeSnapshot(snapshot);

    return scene_pair;
  }

  scenic_impl::input::PointerinjectorRegistry registry_;
};

TEST_F(PointerinjectorRegistryTest, RegisterAttemptWithCorrectArguments_ShouldSucceed) {
  const auto [parent, child] = SetupSceneWithParentAndChildViews();

  fuchsia::ui::pointerinjector::DevicePtr injector;
  bool register_callback_fired = false;
  bool error_callback_fired = false;
  injector.set_error_handler(
      [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });
  {
    fuchsia::ui::pointerinjector::Config config = ConfigTemplate(parent.view_ref, child.view_ref);
    registry_.Register(std::move(config), injector.NewRequest(),
                       [&register_callback_fired] { register_callback_fired = true; });
  }

  RunLoopUntilIdle();

  EXPECT_TRUE(register_callback_fired);
  EXPECT_FALSE(error_callback_fired);
}

TEST_F(PointerinjectorRegistryTest, RegisterAttemptWithBadDeviceConfig_ShouldFail) {
  const auto [parent, child] = SetupSceneWithParentAndChildViews();
  const fuchsia::ui::pointerinjector::Config base_config =
      ConfigTemplate(std::move(parent.view_ref), std::move(child.view_ref));

  {  // No device id.
    fuchsia::ui::pointerinjector::DevicePtr injector;
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerinjector::Config config;
    fidl::Clone(base_config, &config);
    config.clear_device_id();

    registry_.Register(std::move(config), injector.NewRequest(),
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

    registry_.Register(std::move(config), injector.NewRequest(),
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

    registry_.Register(std::move(config), injector.NewRequest(),
                       [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }
}

TEST_F(PointerinjectorRegistryTest, RegisterAttemptWithBadContextOrTarget_ShouldFail) {
  const auto [parent, child] = SetupSceneWithParentAndChildViews();
  const fuchsia::ui::pointerinjector::Config base_config =
      ConfigTemplate(parent.view_ref, child.view_ref);

  {  // No context.
    fuchsia::ui::pointerinjector::DevicePtr injector;
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerinjector::Config config;
    fidl::Clone(base_config, &config);
    config.clear_context();

    registry_.Register(std::move(config), injector.NewRequest(),
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

    registry_.Register(std::move(config), injector.NewRequest(),
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
    fidl::Clone(parent.view_ref, &parent_clone1);
    fidl::Clone(parent.view_ref, &parent_clone2);
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

    registry_.Register(std::move(config), injector.NewRequest(),
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
    fidl::Clone(parent.view_ref, &parent_clone);
    fidl::Clone(child.view_ref, &child_clone);

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

    registry_.Register(std::move(config), injector.NewRequest(),
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
    fidl::Clone(child.view_ref, &child_clone);
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

    registry_.Register(std::move(config), injector.NewRequest(),
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
    fidl::Clone(parent.view_ref, &parent_clone);
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

    registry_.Register(std::move(config), injector.NewRequest(),
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
    fidl::Clone(parent.view_ref, &parent_clone);
    ViewRef child_clone;
    fidl::Clone(child.view_ref, &child_clone);
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

    // Empty the scene.
    registry_.OnNewViewTreeSnapshot(std::make_shared<view_tree::Snapshot>());

    registry_.Register(std::move(config), injector.NewRequest(),
                       [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }
}

TEST_F(PointerinjectorRegistryTest, RegisterAttemptWithBadDispatchPolicy_ShouldFail) {
  const auto [parent, child] = SetupSceneWithParentAndChildViews();
  const fuchsia::ui::pointerinjector::Config base_config =
      ConfigTemplate(parent.view_ref, child.view_ref);

  {  // No dispatch policy.
    fuchsia::ui::pointerinjector::DevicePtr injector;
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerinjector::Config config;
    fidl::Clone(base_config, &config);
    config.clear_dispatch_policy();

    registry_.Register(std::move(config), injector.NewRequest(),
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

    registry_.Register(std::move(config), injector.NewRequest(),
                       [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_FALSE(register_callback_fired);
    EXPECT_TRUE(error_callback_fired);
  }
}

TEST_F(PointerinjectorRegistryTest, ChannelDying_ShouldNotCrash) {
  const auto [parent, child] = SetupSceneWithParentAndChildViews();

  {
    fuchsia::ui::pointerinjector::DevicePtr injector;
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

    fuchsia::ui::pointerinjector::Config config = ConfigTemplate(parent.view_ref, child.view_ref);
    registry_.Register(std::move(config), injector.NewRequest(),
                       [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntilIdle();

    EXPECT_TRUE(register_callback_fired);
    EXPECT_FALSE(error_callback_fired);
  }  // |injector| goes out of scope.

  RunLoopUntilIdle();
}

TEST_F(PointerinjectorRegistryTest, MultipleRegistrations_ShouldSucceed) {
  const auto [parent, child] = SetupSceneWithParentAndChildViews();

  fuchsia::ui::pointerinjector::DevicePtr injector;
  {
    bool register_callback_fired = false;
    bool error_callback_fired = false;
    injector.set_error_handler(
        [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });
    fuchsia::ui::pointerinjector::Config config = ConfigTemplate(parent.view_ref, child.view_ref);
    registry_.Register(std::move(config), injector.NewRequest(),
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

    fuchsia::ui::pointerinjector::Config config = ConfigTemplate(parent.view_ref, child.view_ref);
    registry_.Register(std::move(config), injector2.NewRequest(),
                       [&register_callback_fired] { register_callback_fired = true; });
    RunLoopUntilIdle();
    EXPECT_TRUE(register_callback_fired);
    EXPECT_FALSE(error_callback_fired);
  }
}

// Parameterized tests for malformed viewport arguments.
// Use pairs of optional extents and matrices. Because test parameters must be copyable.
using ViewportPair = std::pair<std::optional<std::array<std::array<float, 2>, 2>>,
                               std::optional<std::array<float, 9>>>;

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

class ParameterizedPointerinjectorRegistryTest : public PointerinjectorRegistryTest,
                                                 public testing::WithParamInterface<ViewportPair> {
};

INSTANTIATE_TEST_SUITE_P(RegisterAttemptWithBadViewport_ShouldFail,
                         ParameterizedPointerinjectorRegistryTest,
                         testing::ValuesIn(BadViewportTestData()));

TEST_P(ParameterizedPointerinjectorRegistryTest, RegisterAttemptWithBadViewport_ShouldFail) {
  const auto [parent, child] = SetupSceneWithParentAndChildViews();

  fuchsia::ui::pointerinjector::DevicePtr injector;
  bool register_callback_fired = false;
  bool error_callback_fired = false;
  injector.set_error_handler(
      [&error_callback_fired](zx_status_t status) { error_callback_fired = true; });

  fuchsia::ui::pointerinjector::Config config =
      ConfigTemplate(std::move(parent.view_ref), std::move(child.view_ref));
  {
    ViewportPair params = GetParam();
    fuchsia::ui::pointerinjector::Viewport viewport;
    if (params.first)
      viewport.set_extents(params.first.value());
    if (params.second)
      viewport.set_viewport_to_context_transform(params.second.value());
    config.set_viewport(std::move(viewport));
  }

  registry_.Register(std::move(config), injector.NewRequest(),
                     [&register_callback_fired] { register_callback_fired = true; });

  RunLoopUntilIdle();

  EXPECT_FALSE(register_callback_fired);
  EXPECT_TRUE(error_callback_fired);
}

}  // namespace input::test
