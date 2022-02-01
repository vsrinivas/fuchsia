// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/termination_reason.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/lib/ui/base_view/embedded_view_utils.h"
#include "src/ui/a11y/lib/semantics/tests/semantics_integration_test_fixture_v2.h"
#include "src/ui/testing/views/embedder_view.h"

namespace accessibility_test {
namespace {

using component_testing::ChildRef;
using component_testing::LocalComponent;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Route;

class FlutterSemanticsTests : public SemanticsIntegrationTestV2 {
 public:
  static constexpr auto kFlutter = "flutter";
  static constexpr auto kFlutterRef = ChildRef{kFlutter};
  static constexpr auto kClientUrl = "fuchsia-pkg://fuchsia.com/a11y-demo#meta/a11y-demo.cmx";

  FlutterSemanticsTests() = default;
  ~FlutterSemanticsTests() override = default;

  void SetUp() override {
    SemanticsIntegrationTestV2::SetUp();

    view_manager()->SetSemanticsEnabled(true);
    FX_LOGS(INFO) << "Launching client view";
    LaunchClient("flutter");
    FX_LOGS(INFO) << "Client view launched";
    RunLoopUntil([&] {
      auto node = view_manager()->GetSemanticNode(view_ref_koid(), 0u);
      return node != nullptr && node->has_attributes() && node->attributes().has_label();
    });
  }

  void ConfigureRealm(RealmBuilder* realm_builder) override {
    // First, add all child components of this test suite.
    realm_builder->AddLegacyChild(kFlutter, kClientUrl);

    // Second, add all necessary routing.
    realm_builder->AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                                  .source = kFlutterRef,
                                  .targets = {ParentRef()}});
    realm_builder->AddRoute(Route{
        .capabilities = {Protocol{fuchsia::accessibility::semantics::SemanticsManager::Name_}},
        .source = kSemanticsManagerRef,
        .targets = {kFlutterRef}});
    realm_builder->AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                                  .source = kScenicRef,
                                  .targets = {kFlutterRef}});
    realm_builder->AddRoute(Route{.capabilities = {Protocol{fuchsia::sys::Environment::Name_}},
                                  .source = ParentRef(),
                                  .targets = {kFlutterRef}});
    realm_builder->AddRoute(
        Route{.capabilities = {Protocol{fuchsia::vulkan::loader::Loader::Name_}},
              .source = ParentRef(),
              .targets = {kFlutterRef}});
    realm_builder->AddRoute(
        Route{.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_}},
              .source = ParentRef(),
              .targets = {kFlutterRef}});
    realm_builder->AddRoute(Route{.capabilities = {Protocol{fuchsia::sysmem::Allocator::Name_}},
                                  .source = ParentRef(),
                                  .targets = {kFlutterRef}});
  }
};

// Loads ally-demo flutter app and verifies its semantic tree.
TEST_F(FlutterSemanticsTests, StaticSemantics) {
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
TEST_F(FlutterSemanticsTests, DISABLED_HitTesting) {
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
TEST_F(FlutterSemanticsTests, PerformAction) {
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
TEST_F(FlutterSemanticsTests, DISABLED_ScrollToMakeVisible) {
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
