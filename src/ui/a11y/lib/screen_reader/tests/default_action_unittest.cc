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
#include "src/ui/a11y/lib/screen_reader/focus/tests/mocks/mock_a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_screen_reader_context.h"
#include "src/ui/a11y/lib/screen_reader/tests/screen_reader_action_test_fixture.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantics_source.h"

namespace accessibility_test {
namespace {

using fuchsia::accessibility::semantics::Attributes;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::Role;

class DefaultActionTest : public ScreenReaderActionTest {
 public:
  DefaultActionTest() = default;
  ~DefaultActionTest() = default;

  void SetUp() override {
    ScreenReaderActionTest::SetUp();

    // Update focused node.
    mock_a11y_focus_manager()->SetA11yFocus(mock_semantic_provider()->koid(), 0u,
                                            [](bool result) { EXPECT_TRUE(result); });
  }
};

// Tests the case when Hit testing results a valid node and OnAccessibilityActionRequested is
// called.
TEST_F(DefaultActionTest, OnAccessibilitActionRequestedCalled) {
  // Creating test node to update.
  uint32_t node_id = 0;
  Node node = CreateTestNode(node_id, "Label A");
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(node));

  a11y::ScreenReaderContext* context = mock_screen_reader_context();
  a11y::DefaultAction default_action(action_context(), context);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  // Update focused node.
  mock_a11y_focus_manager()->SetA11yFocus(mock_semantic_provider()->koid(), node_id,
                                          [](bool result) { EXPECT_TRUE(result); });

  // Call DefaultAction Run()
  default_action.Run(gesture_context);
  RunLoopUntilIdle();

  ASSERT_TRUE(mock_a11y_focus_manager()->IsGetA11yFocusCalled());
  const auto& requested_actions =
      mock_semantics_source()->GetRequestedActionsForView(mock_semantic_provider()->koid());
  EXPECT_EQ(requested_actions.size(), 1u);
  EXPECT_EQ(requested_actions[0].first, node_id);
  EXPECT_EQ(requested_actions[0].second, fuchsia::accessibility::semantics::Action::DEFAULT);
}

// Tests the case when Hit testing doesn't returns a valid node and OnAccessibilityActionRequested
// is not called.
TEST_F(DefaultActionTest, OnAccessibilitActionRequestedNotCalled) {
  // Creating test node to update.
  uint32_t node_id = 0;
  Node node = CreateTestNode(node_id, "Label A");
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(node));

  a11y::ScreenReaderContext* context = mock_screen_reader_context();
  a11y::DefaultAction default_action(action_context(), context);
  a11y::GestureContext gesture_context;

  // Update focused node.
  mock_a11y_focus_manager()->SetA11yFocus(ZX_KOID_INVALID, node_id, [](bool result) {});

  // Call DefaultAction Run()
  default_action.Run(gesture_context);
  RunLoopUntilIdle();

  ASSERT_TRUE(mock_a11y_focus_manager()->IsGetA11yFocusCalled());
  const auto& requested_actions =
      mock_semantics_source()->GetRequestedActionsForView(mock_semantic_provider()->koid());
  EXPECT_TRUE(requested_actions.empty());
}

}  // namespace
}  // namespace accessibility_test
