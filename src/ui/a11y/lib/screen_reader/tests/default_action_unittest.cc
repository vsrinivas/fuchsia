// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/default_action.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <zircon/types.h>

#include <memory>

#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/annotation/tests/mocks/mock_annotation_view.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_registry.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_requester.h"
#include "src/ui/a11y/lib/screen_reader/focus/tests/mocks/mock_a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_screen_reader_context.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantics_event_manager.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_semantics.h"

namespace accessibility_test {
namespace {

using fuchsia::accessibility::semantics::Attributes;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::Role;

class DefaultActionTest : public gtest::TestLoopFixture {
 public:
  DefaultActionTest()
      : view_manager_(std::make_unique<a11y::SemanticTreeServiceFactory>(),
                      std::make_unique<MockViewSemanticsFactory>(),
                      std::make_unique<MockAnnotationViewFactory>(),
                      std::make_unique<MockSemanticsEventManager>(), context_provider_.context(),
                      context_provider_.context()->outgoing()->debug_dir()),
        semantic_provider_(&view_manager_) {
    action_context_.semantics_source = &view_manager_;

    screen_reader_context_ = std::make_unique<MockScreenReaderContext>();
    a11y_focus_manager_ptr_ = screen_reader_context_->mock_a11y_focus_manager_ptr();
    view_manager_.SetSemanticsEnabled(true);
  }

  vfs::PseudoDir* debug_dir() { return context_provider_.context()->outgoing()->debug_dir(); }

  sys::testing::ComponentContextProvider context_provider_;
  a11y::ViewManager view_manager_;
  a11y::ScreenReaderAction::ActionContext action_context_;
  std::unique_ptr<MockScreenReaderContext> screen_reader_context_;
  MockA11yFocusManager* a11y_focus_manager_ptr_;
  accessibility_test::MockSemanticProvider semantic_provider_;
};

// Tests the case when Hit testing results a valid node and OnAccessibilityActionRequested is
// called.
TEST_F(DefaultActionTest, OnAccessibilitActionRequestedCalled) {
  // Creating test node to update.
  std::vector<Node> update_nodes;
  uint32_t node_id = 0;
  Node node = CreateTestNode(node_id, "Label A");
  update_nodes.push_back(std::move(node));

  // Update the node created above.
  semantic_provider_.UpdateSemanticNodes(std::move(update_nodes));
  RunLoopUntilIdle();

  // Commit nodes.
  semantic_provider_.CommitUpdates();
  RunLoopUntilIdle();
  a11y::ScreenReaderContext* context = screen_reader_context_.get();
  a11y::DefaultAction default_action(&action_context_, context);
  a11y::DefaultAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();

  semantic_provider_.SetRequestedAction(fuchsia::accessibility::semantics::Action::SET_FOCUS);

  // Update focused node.
  a11y_focus_manager_ptr_->SetA11yFocus(semantic_provider_.koid(), node_id,
                                        [](bool result) { EXPECT_TRUE(result); });

  // Call DefaultAction Run()
  default_action.Run(action_data);
  RunLoopUntilIdle();

  ASSERT_TRUE(a11y_focus_manager_ptr_->IsGetA11yFocusCalled());
  ASSERT_EQ(fuchsia::accessibility::semantics::Action::DEFAULT,
            semantic_provider_.GetRequestedAction());
  EXPECT_EQ(node_id, semantic_provider_.GetRequestedActionNodeId());
}

// Tests the case when Hit testing doesn't returns a valid node and OnAccessibilityActionRequested
// is not called.
TEST_F(DefaultActionTest, OnAccessibilitActionRequestedNotCalled) {
  // Creating test node to update.
  std::vector<Node> update_nodes;
  uint32_t node_id = 0;
  Node node = CreateTestNode(node_id, "Label A");
  update_nodes.push_back(std::move(node));

  // Update the node created above.
  semantic_provider_.UpdateSemanticNodes(std::move(update_nodes));
  RunLoopUntilIdle();

  // Commit nodes.
  semantic_provider_.CommitUpdates();
  RunLoopUntilIdle();

  a11y::ScreenReaderContext* context = screen_reader_context_.get();
  a11y::DefaultAction default_action(&action_context_, context);
  a11y::DefaultAction::ActionData action_data;

  // Update focused node.
  a11y_focus_manager_ptr_->SetA11yFocus(ZX_KOID_INVALID, node_id, [](bool result) {});

  semantic_provider_.SetRequestedAction(fuchsia::accessibility::semantics::Action::SET_VALUE);

  // Call DefaultAction Run()
  default_action.Run(action_data);
  RunLoopUntilIdle();

  ASSERT_TRUE(a11y_focus_manager_ptr_->IsGetA11yFocusCalled());
  ASSERT_EQ(fuchsia::accessibility::semantics::Action::SET_VALUE,
            semantic_provider_.GetRequestedAction());
  EXPECT_NE(node_id, semantic_provider_.GetRequestedActionNodeId());
}

}  // namespace
}  // namespace accessibility_test
