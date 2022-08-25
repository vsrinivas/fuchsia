// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/decl/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/termination_reason.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/lib/ui/base_view/embedded_view_utils.h"
#include "src/ui/a11y/lib/semantics/tests/semantics_integration_test_fixture.h"

namespace accessibility_test {
namespace {

using component_testing::ChildOptions;
using component_testing::ChildRef;
using component_testing::LocalComponent;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Route;
using component_testing::StartupMode;

class FlutterSemanticsTests : public SemanticsIntegrationTestV2 {
 public:
  static constexpr auto kFlutterJitRunner = "flutter_jit_runner";
  static constexpr auto kFlutterJitRunnerRef = ChildRef{kFlutterJitRunner};
  static constexpr auto kFlutterJitRunnerUrl =
      "fuchsia-pkg://fuchsia.com/flutter_jit_runner#meta/flutter_jit_runner.cm";
  static constexpr auto kFlutterJitProductRunner = "flutter_jit_product_runner";
  static constexpr auto kFlutterJitProductRunnerRef = ChildRef{kFlutterJitProductRunner};
  static constexpr auto kFlutterJitProductRunnerUrl =
      "fuchsia-pkg://fuchsia.com/flutter_jit_product_runner#meta/flutter_jit_product_runner.cm";
  static constexpr auto kFlutterAotRunner = "flutter_aot_runner";
  static constexpr auto kFlutterAotRunnerRef = ChildRef{kFlutterAotRunner};
  static constexpr auto kFlutterAotRunnerUrl =
      "fuchsia-pkg://fuchsia.com/flutter_aot_runner#meta/flutter_aot_runner.cm";
  static constexpr auto kFlutterAotProductRunner = "flutter_aot_product_runner";
  static constexpr auto kFlutterAotProductRunnerRef = ChildRef{kFlutterAotProductRunner};
  static constexpr auto kFlutterAotProductRunnerUrl =
      "fuchsia-pkg://fuchsia.com/flutter_aot_product_runner#meta/flutter_aot_product_runner.cm";
  static constexpr auto kA11yDemo = "flutter";
  static constexpr auto kA11yDemoRef = ChildRef{kA11yDemo};
  static constexpr auto kA11yDemoUrl = "#meta/a11y-demo.cm";
  static constexpr auto kFlutterRunnerEnvironment = "flutter_runner_env";

  FlutterSemanticsTests() = default;
  ~FlutterSemanticsTests() override = default;

  void SetUp() override {
    SemanticsIntegrationTestV2::SetUp();

    SetupScene();

    view_manager()->SetSemanticsEnabled(true);
    RunLoopUntil([&] {
      auto node = view_manager()->GetSemanticNode(view_ref_koid(), 0u);
      return node != nullptr && node->has_attributes() && node->attributes().has_label();
    });
  }

  void ConfigureRealm() override {
    // First, add the flutter runner(s) as children.
    realm()->AddChild(kFlutterJitRunner, kFlutterJitRunnerUrl);
    realm()->AddChild(kFlutterJitProductRunner, kFlutterJitProductRunnerUrl);
    realm()->AddChild(kFlutterAotRunner, kFlutterAotRunnerUrl);
    realm()->AddChild(kFlutterAotProductRunner, kFlutterAotProductRunnerUrl);

    // Then, add an environment providing them.
    fuchsia::component::decl::Environment flutter_runner_environment;
    flutter_runner_environment.set_name(kFlutterRunnerEnvironment);
    flutter_runner_environment.set_extends(fuchsia::component::decl::EnvironmentExtends::REALM);
    flutter_runner_environment.set_runners({});
    auto environment_runners = flutter_runner_environment.mutable_runners();
    fuchsia::component::decl::RunnerRegistration flutter_jit_runner_reg;
    flutter_jit_runner_reg.set_source(fuchsia::component::decl::Ref::WithChild(
        fuchsia::component::decl::ChildRef{.name = kFlutterJitRunner}));
    flutter_jit_runner_reg.set_source_name(kFlutterJitRunner);
    flutter_jit_runner_reg.set_target_name(kFlutterJitRunner);
    environment_runners->push_back(std::move(flutter_jit_runner_reg));
    fuchsia::component::decl::RunnerRegistration flutter_jit_product_runner_reg;
    flutter_jit_product_runner_reg.set_source(fuchsia::component::decl::Ref::WithChild(
        fuchsia::component::decl::ChildRef{.name = kFlutterJitProductRunner}));
    flutter_jit_product_runner_reg.set_source_name(kFlutterJitProductRunner);
    flutter_jit_product_runner_reg.set_target_name(kFlutterJitProductRunner);
    environment_runners->push_back(std::move(flutter_jit_product_runner_reg));
    fuchsia::component::decl::RunnerRegistration flutter_aot_runner_reg;
    flutter_aot_runner_reg.set_source(fuchsia::component::decl::Ref::WithChild(
        fuchsia::component::decl::ChildRef{.name = kFlutterAotRunner}));
    flutter_aot_runner_reg.set_source_name(kFlutterAotRunner);
    flutter_aot_runner_reg.set_target_name(kFlutterAotRunner);
    environment_runners->push_back(std::move(flutter_aot_runner_reg));
    fuchsia::component::decl::RunnerRegistration flutter_aot_product_runner_reg;
    flutter_aot_product_runner_reg.set_source(fuchsia::component::decl::Ref::WithChild(
        fuchsia::component::decl::ChildRef{.name = kFlutterAotProductRunner}));
    flutter_aot_product_runner_reg.set_source_name(kFlutterAotProductRunner);
    flutter_aot_product_runner_reg.set_target_name(kFlutterAotProductRunner);
    environment_runners->push_back(std::move(flutter_aot_product_runner_reg));
    auto realm_decl = realm()->GetRealmDecl();
    if (!realm_decl.has_environments()) {
      realm_decl.set_environments({});
    }
    auto realm_environments = realm_decl.mutable_environments();
    realm_environments->push_back(std::move(flutter_runner_environment));
    realm()->ReplaceRealmDecl(std::move(realm_decl));

    // Then, add all child components of this test suite.
    realm()->AddChild(kA11yDemo, kA11yDemoUrl,
                      ChildOptions{
                          .environment = kFlutterRunnerEnvironment,
                      });

    // Finally, add all necessary routing.
    // Required services are routed through ui test manager realm to client
    // subrealm. Consume them from parent.
    realm()->AddRoute(Route{.capabilities =
                                {
                                    Protocol{fuchsia::logger::LogSink::Name_},
                                    Protocol{fuchsia::sysmem::Allocator::Name_},
                                    Protocol{fuchsia::tracing::provider::Registry::Name_},
                                    Protocol{fuchsia::ui::scenic::Scenic::Name_},
                                    Protocol{fuchsia::vulkan::loader::Loader::Name_},
                                },
                            .source = ParentRef{},
                            .targets = {kFlutterJitRunnerRef, kFlutterJitProductRunnerRef,
                                        kFlutterAotRunnerRef, kFlutterAotProductRunnerRef}});
    realm()->AddRoute(
        Route{.capabilities =
                  {
                      Protocol{fuchsia::accessibility::semantics::SemanticsManager::Name_},
                  },
              .source = kSemanticsManagerRef,
              .targets = {kFlutterJitRunnerRef, kFlutterJitProductRunnerRef, kFlutterAotRunnerRef,
                          kFlutterAotProductRunnerRef}});
    realm()->AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                            .source = kA11yDemoRef,
                            .targets = {ParentRef()}});
  }
};

INSTANTIATE_TEST_SUITE_P(FlutterSemanticsTestWithParams, FlutterSemanticsTests,
                         ::testing::ValuesIn(SemanticsIntegrationTestV2::UIConfigurationsToTest()));

// Loads ally-demo flutter app and verifies its semantic tree.
TEST_P(FlutterSemanticsTests, StaticSemantics) {
  auto root = view_manager()->GetSemanticNode(view_ref_koid(), 0u);
  auto node = FindNodeWithLabel(root, view_ref_koid(), "Blue tapped 0 times");
  ASSERT_TRUE(node);

  node = FindNodeWithLabel(root, view_ref_koid(), "Yellow tapped 0 times");
  ASSERT_TRUE(node);

  node = FindNodeWithLabel(root, view_ref_koid(), "Blue");
  ASSERT_TRUE(node);

  node = FindNodeWithLabel(root, view_ref_koid(), "Yellow");
  ASSERT_TRUE(node);
}

// Loads ally-demo flutter app and validates hit testing
TEST_P(FlutterSemanticsTests, HitTesting) {
  FX_LOGS(INFO) << "Wait for scale factor";
  WaitForScaleFactor();
  FX_LOGS(INFO) << "Received scale factor";

  auto root = view_manager()->GetSemanticNode(view_ref_koid(), 0u);

  // Hit test something with an action
  auto node = FindNodeWithLabel(root, view_ref_koid(), "Blue");
  ASSERT_TRUE(node);
  auto hit_node = HitTest(
      view_ref_koid(), CalculateCenterOfSemanticNodeBoundingBoxCoordinate(view_ref_koid(), node));
  ASSERT_TRUE(hit_node.has_value());
  ASSERT_EQ(*hit_node, node->node_id());

  // Hit test a label
  node = FindNodeWithLabel(root, view_ref_koid(), "Yellow tapped 0 times");
  ASSERT_TRUE(node);
  hit_node = HitTest(view_ref_koid(),
                     CalculateCenterOfSemanticNodeBoundingBoxCoordinate(view_ref_koid(), node));
  ASSERT_TRUE(hit_node.has_value());
  ASSERT_EQ(*hit_node, node->node_id());
}

// Loads ally-demo flutter app and validates triggering actions
TEST_P(FlutterSemanticsTests, PerformAction) {
  auto root = view_manager()->GetSemanticNode(view_ref_koid(), 0u);

  // Verify the counter is currently at 0
  auto node = FindNodeWithLabel(root, view_ref_koid(), "Blue tapped 0 times");
  EXPECT_TRUE(node);

  // Trigger the button's default action
  node = FindNodeWithLabel(root, view_ref_koid(), "Blue");
  ASSERT_TRUE(node);
  bool callback_handled = PerformAccessibilityAction(
      view_ref_koid(), node->node_id(), fuchsia::accessibility::semantics::Action::DEFAULT);
  EXPECT_TRUE(callback_handled);

  // Verify the counter is now at 1
  // TODO(fxb.dev/58276): Once we have the Semantic Event Updates work done, this logic can be
  // more clearly written as waiting for notification of an update then checking the tree.
  RunLoopUntil([this, root] {
    auto node = FindNodeWithLabel(root, view_ref_koid(), "Blue tapped 1 time");
    return node != nullptr;
  });
}

// Loads ally-demo flutter app and validates scroll-to-make-visible
TEST_P(FlutterSemanticsTests, ScrollToMakeVisible) {
  FX_LOGS(INFO) << "Wait for scale factor";
  WaitForScaleFactor();
  FX_LOGS(INFO) << "Received scale factor";

  auto root = view_manager()->GetSemanticNode(view_ref_koid(), 0u);

  // The "Yellow" node should be off-screen in a scrollable list
  auto node = FindNodeWithLabel(root, view_ref_koid(), "Yellow");
  ASSERT_TRUE(node);
  // Record the location of a corner of the node's bounding box.  We record this rather than the
  // transform or the location fields since the runtime could change either when an element is
  // moved.
  auto node_corner =
      GetTransformForNode(view_ref_koid(), node->node_id()).Apply(node->location().min);

  bool callback_handled = PerformAccessibilityAction(
      view_ref_koid(), node->node_id(), fuchsia::accessibility::semantics::Action::SHOW_ON_SCREEN);
  EXPECT_TRUE(callback_handled);

  // Verify the "Yellow" node has moved
  // TODO(fxb.dev/58276): Once we have the Semantic Event Updates work done, this logic can be
  // more clearly written as waiting for notification of an update then checking the tree.
  RunLoopUntil([this, root, &node_corner] {
    auto node = FindNodeWithLabel(root, view_ref_koid(), "Yellow");
    if (node == nullptr) {
      return false;
    }

    auto new_node_corner =
        GetTransformForNode(view_ref_koid(), node->node_id()).Apply(node->location().min);
    return node_corner.x != new_node_corner.x || node_corner.y != new_node_corner.y ||
           node_corner.z != new_node_corner.z;
  });
}
}  // namespace
}  // namespace accessibility_test
