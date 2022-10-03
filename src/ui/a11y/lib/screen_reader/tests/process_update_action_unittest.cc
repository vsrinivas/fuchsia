// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/process_update_action.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/async/cpp/time.h>
#include <lib/async/default.h>
#include <zircon/types.h>

#include <memory>

#include <gmock/gmock.h>

#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_screen_reader_context.h"
#include "src/ui/a11y/lib/screen_reader/tests/screen_reader_action_test_fixture.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantics_source.h"

namespace accessibility_test {
namespace {

using a11y::ScreenReaderContext;

class ProcessUpdateActionTest : public ScreenReaderActionTest {
 public:
  ProcessUpdateActionTest() = default;
  ~ProcessUpdateActionTest() override = default;

  void SetUp() override {
    ScreenReaderActionTest::SetUp();

    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(0u);
    node.set_role(fuchsia::accessibility::semantics::Role::TEXT_FIELD);
    node.mutable_attributes()->set_label("node 1");
    node.mutable_child_ids()->push_back(1);

    fuchsia::accessibility::semantics::Node node2;
    node2.set_node_id(1u);
    node2.mutable_attributes()->set_label("node2");

    fuchsia::accessibility::semantics::Node node3;
    node3.set_node_id(2u);

    mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(node));
    mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(node2));
    mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(node3));
  }
};

TEST_F(ProcessUpdateActionTest, HasRegisteredOnNodeUpdateCallback) {
  mock_a11y_focus_manager()->SetA11yFocus(mock_semantic_provider()->koid(), 0,
                                          [](bool result) { EXPECT_TRUE(result); });
  bool callback_ran = false;
  mock_screen_reader_context()->set_on_node_update_callback(
      [&callback_ran](auto...) { callback_ran = true; });
  a11y::ProcessUpdateAction action(action_context(), mock_screen_reader_context());
  action.Run({});
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_ran);
  EXPECT_FALSE(mock_speaker()->ReceivedSpeak());
}

TEST_F(ProcessUpdateActionTest, ChangeInDescribableContentOfFocusedNodeCausesNodeToBeSpoken) {
  mock_screen_reader_context()->set_last_interaction(async::Now(async_get_default_dispatcher()));
  mock_a11y_focus_manager()->SetA11yFocus(mock_semantic_provider()->koid(), 0,
                                          [](bool result) { EXPECT_TRUE(result); });
  mock_screen_reader_context()->set_describable_content_changed(true);
  a11y::ProcessUpdateAction action(action_context(), mock_screen_reader_context());
  action.Run({});
  RunLoopUntilIdle();
  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  ASSERT_EQ(mock_speaker()->node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->node_ids()[0], 0u);
}

TEST_F(ProcessUpdateActionTest, TtsShouldBeNonInterrupting) {
  mock_screen_reader_context()->set_last_interaction(async::Now(async_get_default_dispatcher()));
  mock_a11y_focus_manager()->SetA11yFocus(mock_semantic_provider()->koid(), 0,
                                          [](bool result) { EXPECT_TRUE(result); });
  mock_screen_reader_context()->set_describable_content_changed(true);
  a11y::ProcessUpdateAction action(action_context(), mock_screen_reader_context());
  action.Run({});
  RunLoopUntilIdle();
  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());

  ASSERT_EQ(mock_speaker()->node_ids().size(), 1u);
  ASSERT_EQ(mock_speaker()->speak_node_options().size(), 1u);
  // Most screen reader TTSes are interrupting, but the TTSes from this action
  // should be noninterrupting.
  EXPECT_EQ(mock_speaker()->speak_node_options()[0].interrupt, false);
}

TEST_F(ProcessUpdateActionTest, NoChangeInDescribableContentOfFocusedNodeCausesNoOutput) {
  mock_screen_reader_context()->set_last_interaction(async::Now(async_get_default_dispatcher()));
  mock_a11y_focus_manager()->SetA11yFocus(mock_semantic_provider()->koid(), 0,
                                          [](bool result) { EXPECT_TRUE(result); });
  mock_screen_reader_context()->set_describable_content_changed(false);
  a11y::ProcessUpdateAction action(action_context(), mock_screen_reader_context());
  action.Run({});
  RunLoopUntilIdle();
  EXPECT_FALSE(mock_speaker()->ReceivedSpeak());
}

TEST_F(ProcessUpdateActionTest, FrequentNodeUpdatesRespectDelayOfOutputs) {
  mock_screen_reader_context()->set_last_interaction(async::Now(async_get_default_dispatcher()));
  mock_a11y_focus_manager()->SetA11yFocus(mock_semantic_provider()->koid(), 0,
                                          [](bool result) { EXPECT_TRUE(result); });
  mock_screen_reader_context()->set_describable_content_changed(true);
  a11y::ProcessUpdateAction action(action_context(), mock_screen_reader_context());
  action.Run({});
  RunLoopUntilIdle();
  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  ASSERT_EQ(mock_speaker()->node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->node_ids()[0], 0u);
  RunLoopFor(zx::msec(50));
  action.Run({});
  RunLoopUntilIdle();
  // No extra output should be spoken at this point.
  ASSERT_EQ(mock_speaker()->node_ids().size(), 1u);
  RunLoopFor(zx::sec(1));
  action.Run({});
  RunLoopUntilIdle();
  ASSERT_EQ(mock_speaker()->node_ids().size(), 2u);
}

TEST_F(ProcessUpdateActionTest, FocusedNodeIsNotDescribable) {
  mock_screen_reader_context()->set_last_interaction(async::Now(async_get_default_dispatcher()));
  mock_a11y_focus_manager()->SetA11yFocus(mock_semantic_provider()->koid(), 2u,
                                          [](bool result) { EXPECT_TRUE(result); });
  mock_screen_reader_context()->set_describable_content_changed(true);
  a11y::ProcessUpdateAction action(action_context(), mock_screen_reader_context());
  action.Run({});
  RunLoopUntilIdle();
  EXPECT_FALSE(mock_speaker()->ReceivedSpeak());
}

TEST_F(ProcessUpdateActionTest, AvoidsSpeakingWhenuserIsNotActive) {
  mock_screen_reader_context()->set_last_interaction(async::Now(async_get_default_dispatcher()));
  mock_a11y_focus_manager()->SetA11yFocus(mock_semantic_provider()->koid(), 0,
                                          [](bool result) { EXPECT_TRUE(result); });
  mock_screen_reader_context()->set_describable_content_changed(true);
  a11y::ProcessUpdateAction action(action_context(), mock_screen_reader_context());
  action.Run({});
  RunLoopUntilIdle();
  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  ASSERT_EQ(mock_speaker()->node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->node_ids()[0], 0u);
  RunLoopFor(zx::min(6));
  action.Run({});
  RunLoopUntilIdle();
  // No extra output should be spoken at this point.
  ASSERT_EQ(mock_speaker()->node_ids().size(), 1u);
}

}  // namespace
}  // namespace accessibility_test
