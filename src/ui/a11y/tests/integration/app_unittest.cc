// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/bin/a11y_manager/app.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/vfs/cpp/pseudo_dir.h>

#include "gtest/gtest.h"
#include "src/ui/a11y/bin/a11y_manager/util.h"
#include "src/ui/a11y/tests/mocks/mock_semantic_listener.h"
#include "src/ui/a11y/tests/mocks/mock_settings_provider.h"
#include "src/ui/a11y/tests/util/util.h"

namespace accessibility_test {
namespace {

using fuchsia::accessibility::SettingsManagerStatus;
using fuchsia::accessibility::SettingsPtr;
using fuchsia::accessibility::semantics::Attributes;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::NodePtr;
using fuchsia::accessibility::semantics::Role;

const std::string kSemanticTreeSingle = "Node_id: 0, Label:Label A";
constexpr int kMaxLogBufferSize = 1024;

// clang-format off
constexpr std::array<float, 9> kIdentityMatrix = {
    1, 0, 0,
    0, 1, 0,
    0, 0, 1};

// clang-format on

class AppUnitTest : public gtest::TestLoopFixture {
 public:
  AppUnitTest() { context_ = context_provider_.context(); }
  void SetUp() override {
    TestLoopFixture::SetUp();
    zx::eventpair a, b;
    zx::eventpair::create(0u, &a, &b);
    view_ref_ = fuchsia::ui::views::ViewRef({
        .reference = std::move(a),
    });
  }

  sys::ComponentContext *context_;
  sys::testing::ComponentContextProvider context_provider_;
  fuchsia::ui::views::ViewRef view_ref_;
};

// Create a test node with only a node id and a label.
Node CreateTestNode(uint32_t node_id, std::string label) {
  Node node = Node();
  node.set_node_id(node_id);
  node.set_child_ids(fidl::VectorPtr<uint32_t>::New(0));
  node.set_role(Role::UNKNOWN);
  node.set_attributes(Attributes());
  node.mutable_attributes()->set_label(std::move(label));
  fuchsia::ui::gfx::BoundingBox box;
  node.set_location(std::move(box));
  fuchsia::ui::gfx::mat4 transform;
  node.set_transform(std::move(transform));
  return node;
}

// Test to make sure SemanticsManager Service is exposed by A11y.
// Test sends a node update to SemanticsManager and then compare the expected
// result using log file created by semantics manager.
TEST_F(AppUnitTest, UpdateNodeToSemanticsManager) {
  a11y_manager::App app = a11y_manager::App(context_provider_.TakeContext());
  RunLoopUntilIdle();

  // Create ViewRef.
  fuchsia::ui::views::ViewRef view_ref_connection;
  fidl::Clone(view_ref_, &view_ref_connection);

  // Create ActionListener.
  accessibility_test::MockSemanticListener semantic_listener(&context_provider_,
                                                             std::move(view_ref_connection));
  // We make sure the Semantic Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  // Creating test node to update.
  std::vector<Node> update_nodes;
  Node node = CreateTestNode(0, "Label A");
  Node clone_node;
  node.Clone(&clone_node);
  update_nodes.push_back(std::move(clone_node));

  // Update the node created above.
  semantic_listener.UpdateSemanticNodes(std::move(update_nodes));
  RunLoopUntilIdle();

  // Commit nodes.
  semantic_listener.Commit();
  RunLoopUntilIdle();

  // Check that the committed node is present in the semantic tree.
  vfs::PseudoDir *debug_dir = context_->outgoing()->debug_dir();
  vfs::internal::Node *test_node;
  ASSERT_EQ(ZX_OK, debug_dir->Lookup(std::to_string(a11y_manager::GetKoid(view_ref_)), &test_node));

  char buffer[kMaxLogBufferSize];
  ReadFile(test_node, kSemanticTreeSingle.size(), buffer);
  EXPECT_EQ(kSemanticTreeSingle, buffer);
}

// Test to make sure SettingsManager Service is exposed by A11y.
// Test sends connects a fake settings provider to SettingsManager, and make
// sure App gets the updates.
TEST_F(AppUnitTest, VerifyAppSettingsWatcher) {
  a11y_manager::App app = a11y_manager::App(context_provider_.TakeContext());
  RunLoopUntilIdle();

  // Create Settings Service.
  MockSettingsProvider settings_provider(&context_provider_);
  RunLoopUntilIdle();

  // Verify default values of settings in App.
  float kDefaultZoomFactor = 1.0;
  SettingsPtr settings = app.GetSettings();
  EXPECT_TRUE(settings->has_magnification_enabled());
  EXPECT_FALSE(settings->magnification_enabled());
  EXPECT_TRUE(settings->has_magnification_zoom_factor());
  EXPECT_EQ(kDefaultZoomFactor, settings->magnification_zoom_factor());
  EXPECT_TRUE(settings->has_screen_reader_enabled());
  EXPECT_FALSE(settings->screen_reader_enabled());
  EXPECT_TRUE(settings->has_color_inversion_enabled());
  EXPECT_FALSE(settings->color_inversion_enabled());
  EXPECT_TRUE(settings->has_color_correction());
  EXPECT_EQ(fuchsia::accessibility::ColorCorrection::DISABLED, settings->color_correction());
  EXPECT_TRUE(settings->has_color_adjustment_matrix());
  EXPECT_EQ(kIdentityMatrix, settings->color_adjustment_matrix());

  // Change settings and verify the changes are reflected in App.
  SettingsManagerStatus status = SettingsManagerStatus::OK;
  settings_provider.SetMagnificationEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ(status, SettingsManagerStatus::OK);
  settings_provider.SetMagnificationZoomFactor(
      10, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ(status, SettingsManagerStatus::OK);
  settings_provider.SetScreenReaderEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ(status, SettingsManagerStatus::OK);
  settings_provider.SetColorInversionEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ(status, SettingsManagerStatus::OK);
  settings_provider.SetColorCorrection(
      fuchsia::accessibility::ColorCorrection::CORRECT_PROTANOMALY,
      [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ(status, SettingsManagerStatus::OK);

  // Verify new settings in App.
  float kExpectedZoomFactor = 10.0;
  settings = app.GetSettings();
  EXPECT_TRUE(settings->has_magnification_enabled());
  EXPECT_TRUE(settings->magnification_enabled());
  EXPECT_TRUE(settings->has_magnification_zoom_factor());
  EXPECT_EQ(kExpectedZoomFactor, settings->magnification_zoom_factor());
  EXPECT_TRUE(settings->has_screen_reader_enabled());
  EXPECT_TRUE(settings->screen_reader_enabled());
  EXPECT_TRUE(settings->has_color_inversion_enabled());
  EXPECT_TRUE(settings->color_inversion_enabled());
  EXPECT_TRUE(settings->has_color_correction());
  EXPECT_EQ(fuchsia::accessibility::ColorCorrection::CORRECT_PROTANOMALY,
            settings->color_correction());
  EXPECT_TRUE(settings->has_color_adjustment_matrix());
}

}  // namespace
}  // namespace accessibility_test
