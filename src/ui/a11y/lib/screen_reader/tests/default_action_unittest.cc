// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/default_action.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"

namespace accessibility_test {
namespace {

using fuchsia::accessibility::semantics::Attributes;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::Role;

const std::string kSemanticTreeSingle = "Node_id: 0, Label:Label A";
constexpr int kMaxLogBufferSize = 1024;

class DefaultActionTest : public gtest::TestLoopFixture {
 public:
  DefaultActionTest()
      : semantics_manager_(std::make_unique<a11y::SemanticTreeServiceFactory>(),
                           context_provider_.context()->outgoing()->debug_dir()),
        semantic_provider_(&semantics_manager_) {
    action_context_.semantics_manager = &semantics_manager_;
    semantics_manager_.SetSemanticsManagerEnabled(true);
  }

  vfs::PseudoDir* debug_dir() { return context_provider_.context()->outgoing()->debug_dir(); }

  sys::testing::ComponentContextProvider context_provider_;
  a11y::SemanticsManager semantics_manager_;
  a11y::ScreenReaderAction::ActionContext action_context_;
  accessibility_test::MockSemanticProvider semantic_provider_;
};

// Create a test node with only a node id and a label.
Node CreateTestNode(uint32_t node_id, std::string label) {
  Node node = Node();
  node.set_node_id(node_id);
  node.set_child_ids({});
  node.set_role(Role::UNKNOWN);
  node.set_attributes(Attributes());
  node.mutable_attributes()->set_label(std::move(label));
  fuchsia::ui::gfx::BoundingBox box;
  node.set_location(box);
  fuchsia::ui::gfx::mat4 transform;
  node.set_transform(transform);
  return node;
}

// Tests the case when Hit testing results a valid node and OnAccessibilityActionRequested is
// called.
TEST_F(DefaultActionTest, OnAccessibilitActionRequestedCalled) {
  // Creating test node to update.
  std::vector<Node> update_nodes;
  Node node = CreateTestNode(0, "Label A");
  update_nodes.push_back(std::move(node));

  // Update the node created above.
  semantic_provider_.UpdateSemanticNodes(std::move(update_nodes));
  RunLoopUntilIdle();

  // Commit nodes.
  semantic_provider_.CommitUpdates();
  RunLoopUntilIdle();

  // Check that the committed node is present in the semantic tree.
  vfs::internal::Node* test_node;
  ASSERT_EQ(ZX_OK, debug_dir()->Lookup(std::to_string(a11y::GetKoid(semantic_provider_.view_ref())),
                                       &test_node));
  char buffer[kMaxLogBufferSize];
  accessibility_test::ReadFile(test_node, kSemanticTreeSingle.size(), buffer);
  EXPECT_EQ(kSemanticTreeSingle, buffer);

  a11y::DefaultAction default_action(&action_context_);
  a11y::DefaultAction::ActionData action_data;
  action_data.koid = a11y::GetKoid(semantic_provider_.view_ref());

  semantic_provider_.SetRequestedAction(fuchsia::accessibility::semantics::Action::SET_FOCUS);

  // Call DefaultAction Run()
  default_action.Run(action_data);
  RunLoopUntilIdle();

  EXPECT_EQ(fuchsia::accessibility::semantics::Action::DEFAULT,
            semantic_provider_.GetRequestedAction());
}

// Tests the case when Hit testing doesn't returns a valid node and OnAccessibilityActionRequested
// is not called.
TEST_F(DefaultActionTest, OnAccessibilitActionRequestedNotCalled) {
  // Creating test node to update.
  std::vector<Node> update_nodes;
  Node node = CreateTestNode(0, "Label A");
  update_nodes.push_back(std::move(node));

  // Update the node created above.
  semantic_provider_.UpdateSemanticNodes(std::move(update_nodes));
  RunLoopUntilIdle();

  // Commit nodes.
  semantic_provider_.CommitUpdates();
  RunLoopUntilIdle();

  // Check that the committed node is present in the semantic tree.
  vfs::internal::Node* test_node;
  ASSERT_EQ(ZX_OK, debug_dir()->Lookup(std::to_string(a11y::GetKoid(semantic_provider_.view_ref())),
                                       &test_node));
  char buffer[kMaxLogBufferSize];
  accessibility_test::ReadFile(test_node, kSemanticTreeSingle.size(), buffer);
  EXPECT_EQ(kSemanticTreeSingle, buffer);

  a11y::DefaultAction default_action(&action_context_);
  a11y::DefaultAction::ActionData action_data;
  action_data.koid = ZX_KOID_INVALID;

  semantic_provider_.SetRequestedAction(fuchsia::accessibility::semantics::Action::SET_FOCUS);

  // Call DefaultAction Run()
  default_action.Run(action_data);
  RunLoopUntilIdle();

  EXPECT_EQ(fuchsia::accessibility::semantics::Action::SET_FOCUS,
            semantic_provider_.GetRequestedAction());
}

}  // namespace
}  // namespace accessibility_test
