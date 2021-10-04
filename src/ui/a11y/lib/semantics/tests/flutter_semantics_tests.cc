// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/sys/cpp/termination_reason.h>
#include <lib/sys/cpp/testing/realm_builder.h>
#include <lib/sys/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/lib/ui/base_view/embedded_view_utils.h"
#include "src/ui/a11y/lib/semantics/tests/semantics_integration_test_fixture_v2.h"
#include "src/ui/testing/views/embedder_view.h"

namespace accessibility_test {
namespace {

using sys::testing::AboveRoot;
using sys::testing::CapabilityRoute;
using sys::testing::Component;
using sys::testing::LegacyComponentUrl;
using sys::testing::Moniker;
using sys::testing::Protocol;

class FlutterSemanticsTests : public SemanticsIntegrationTestV2 {
 public:
  static constexpr auto kFlutterMoniker = Moniker{"flutter"};
  static constexpr auto kClientUrl =
      LegacyComponentUrl{"fuchsia-pkg://fuchsia.com/a11y-demo#meta/a11y-demo.cmx"};

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

  // Subclass should implement this method to add components to the test realm
  // next to the base ones added.
  std::vector<std::pair<Moniker, Component>> GetTestComponents() override {
    return {std::make_pair(kFlutterMoniker, Component{.source = kClientUrl})};
  }

  // Subclass should implement this method to add capability routes to the test
  // realm next to the base ones added.
  virtual std::vector<CapabilityRoute> GetTestRoutes() override {
    return {{.capability = Protocol{fuchsia::ui::app::ViewProvider::Name_},
             .source = kFlutterMoniker,
             .targets = {AboveRoot()}},
            {.capability = Protocol{fuchsia::accessibility::semantics::SemanticsManager::Name_},
             .source = kSemanticsManagerMoniker,
             .targets = {kFlutterMoniker}},
            {.capability = Protocol{fuchsia::ui::scenic::Scenic::Name_},
             .source = kScenicMoniker,
             .targets = {kFlutterMoniker}},
            {.capability = Protocol{fuchsia::sys::Environment::Name_},
             .source = AboveRoot(),
             .targets = {kFlutterMoniker}},
            {.capability = Protocol{fuchsia::vulkan::loader::Loader::Name_},
             .source = AboveRoot(),
             .targets = {kFlutterMoniker}},
            {.capability = Protocol{fuchsia::tracing::provider::Registry::Name_},
             .source = AboveRoot(),
             .targets = {kFlutterMoniker}},
            {.capability = Protocol{fuchsia::sysmem::Allocator::Name_},
             .source = AboveRoot(),
             .targets = {kFlutterMoniker}}};
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
