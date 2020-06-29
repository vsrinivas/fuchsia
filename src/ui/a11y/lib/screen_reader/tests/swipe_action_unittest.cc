// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/swipe_action.h"

#include <lib/gtest/test_loop_fixture.h>

#include <cstdint>
#include <memory>

#include "lib/sys/cpp/testing/component_context_provider.h"
#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/annotation/tests/mocks/mock_annotation_view.h"
#include "src/ui/a11y/lib/screen_reader/focus/tests/mocks/mock_a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_tts_engine.h"
#include "src/ui/a11y/lib/semantics/semantic_tree.h"
#include "src/ui/a11y/lib/semantics/semantic_tree_service.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_listener.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_tree.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_semantics.h"

namespace accessibility_test {
namespace {

using fuchsia::accessibility::semantics::Attributes;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::Role;

const std::string kRootNodeLabel = "Label A";
const std::string kChildNodeLabel = "Label B";
constexpr uint32_t kRootNodeId = 0;
constexpr uint32_t kChildNodeId = 1;

constexpr auto kNextAction = a11y::SwipeAction::SwipeActionType::kNextAction;
constexpr auto kPreviousAction = a11y::SwipeAction::SwipeActionType::kPreviousAction;

class MockSemanticTreeServiceFactory : public a11y::SemanticTreeServiceFactory {
 public:
  std::unique_ptr<a11y::SemanticTreeService> NewService(
      zx_koid_t koid, fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener,
      vfs::PseudoDir* debug_dir,
      a11y::SemanticTreeService::CloseChannelCallback close_channel_callback) override {
    semantic_tree_ = std::make_unique<MockSemanticTree>();
    semantic_tree_ptr_ = semantic_tree_.get();
    auto service = std::make_unique<a11y::SemanticTreeService>(
        std::move(semantic_tree_), koid, std::move(semantic_listener), debug_dir,
        std::move(close_channel_callback));
    service_ = service.get();

    return service;
  }

  a11y::SemanticTreeService* service() { return service_; }
  MockSemanticTree* semantic_tree() { return semantic_tree_ptr_; }

 private:
  a11y::SemanticTreeService* service_ = nullptr;
  std::unique_ptr<MockSemanticTree> semantic_tree_;
  MockSemanticTree* semantic_tree_ptr_ = nullptr;
};

class SwipeActionTest : public gtest::TestLoopFixture {
 public:
  SwipeActionTest()
      : factory_(std::make_unique<MockSemanticTreeServiceFactory>()),
        factory_ptr_(factory_.get()),
        view_manager_(std::move(factory_), std::make_unique<MockViewSemanticsFactory>(),
                      std::make_unique<MockAnnotationViewFactory>(), context_provider_.context(),
                      context_provider_.context()->outgoing()->debug_dir()),
        tts_manager_(context_provider_.context()),
        semantic_provider_(&view_manager_) {
    action_context_.semantics_source = &view_manager_;
    tts_manager_.OpenEngine(action_context_.tts_engine_ptr.NewRequest(),
                            [](fuchsia::accessibility::tts::TtsManager_OpenEngine_Result result) {
                              EXPECT_TRUE(result.is_response());
                            });
    a11y_focus_manager_ = std::make_unique<MockA11yFocusManager>();
    a11y_focus_manager_ptr_ = a11y_focus_manager_.get();
    screen_reader_context_ =
        std::make_unique<a11y::ScreenReaderContext>(std::move(a11y_focus_manager_), &tts_manager_);
    view_manager_.SetSemanticsEnabled(true);

    SetupTtsEngine(&mock_tts_engine_);
  }

  void SetupTtsEngine(accessibility_test::MockTtsEngine* mock_tts_engine) {
    fidl::InterfaceHandle<fuchsia::accessibility::tts::Engine> engine_handle =
        mock_tts_engine->GetHandle();
    tts_manager_.RegisterEngine(
        std::move(engine_handle),
        [](fuchsia::accessibility::tts::EngineRegistry_RegisterEngine_Result result) {
          EXPECT_TRUE(result.is_response());
        });
    RunLoopUntilIdle();
  }

  void AddNodeToSemanticTree() {
    // Creating test nodes to update.
    Node root_node = CreateTestNode(kRootNodeId, kRootNodeLabel);
    root_node.set_child_ids({kChildNodeId});

    Node child_node = CreateTestNode(kChildNodeId, kChildNodeLabel);
    std::vector<Node> update_nodes;
    update_nodes.push_back(std::move(root_node));
    update_nodes.push_back(std::move(child_node));

    // Update the nodes created above.
    semantic_provider_.UpdateSemanticNodes(std::move(update_nodes));
    RunLoopUntilIdle();

    // Commit nodes.
    semantic_provider_.CommitUpdates();
    RunLoopUntilIdle();
  }

  std::unique_ptr<MockSemanticTreeServiceFactory> factory_;
  MockSemanticTreeServiceFactory* factory_ptr_;
  sys::testing::ComponentContextProvider context_provider_;
  a11y::ViewManager view_manager_;
  a11y::ScreenReaderAction::ActionContext action_context_;
  a11y::TtsManager tts_manager_;
  std::unique_ptr<a11y::ScreenReaderContext> screen_reader_context_;
  std::unique_ptr<MockA11yFocusManager> a11y_focus_manager_;
  MockA11yFocusManager* a11y_focus_manager_ptr_;
  accessibility_test::MockSemanticProvider semantic_provider_;
  accessibility_test::MockTtsEngine mock_tts_engine_;
};

// Swipe Action should do nothing if there is no semantic tree in focus.
TEST_F(SwipeActionTest, NoTreeInFocus) {
  a11y::SwipeAction next_action(&action_context_, screen_reader_context_.get(), kNextAction);
  a11y::SwipeAction::ActionData action_data;

  // Call NextAction Run().
  next_action.Run(action_data);
  RunLoopUntilIdle();

  EXPECT_TRUE(a11y_focus_manager_ptr_->IsGetA11yFocusCalled());
  EXPECT_FALSE(factory_ptr_->semantic_tree()->IsGetNextNodeCalled());
  EXPECT_FALSE(factory_ptr_->semantic_tree()->IsGetPreviousNodeCalled());
  EXPECT_FALSE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());
  EXPECT_FALSE(mock_tts_engine_.ReceivedCancel());
  EXPECT_FALSE(mock_tts_engine_.ReceivedSpeak());
}

// When next node is not found, the Swipe Action should do nothing.
TEST_F(SwipeActionTest, NextNodeNotFound) {
  AddNodeToSemanticTree();

  // Update focused node.
  a11y_focus_manager_ptr_->UpdateA11yFocus(semantic_provider_.koid(), kRootNodeId);

  // Set Next Node result.
  factory_ptr_->semantic_tree()->SetNextNode(nullptr);

  a11y::SwipeAction next_action(&action_context_, screen_reader_context_.get(), kNextAction);
  a11y::SwipeAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();

  // Call NextAction Run().
  next_action.Run(action_data);
  RunLoopUntilIdle();

  EXPECT_TRUE(factory_ptr_->semantic_tree()->IsGetNextNodeCalled());
  EXPECT_EQ(kRootNodeId, factory_ptr_->semantic_tree()->NextNodeCalledOnId());
  EXPECT_FALSE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());
  EXPECT_FALSE(mock_tts_engine_.ReceivedCancel());
  EXPECT_FALSE(mock_tts_engine_.ReceivedSpeak());
}

// When Previous node is not found, the Swipe Action should do nothing.
TEST_F(SwipeActionTest, PreviousNodeNotFound) {
  AddNodeToSemanticTree();

  // Update focused node.
  a11y_focus_manager_ptr_->UpdateA11yFocus(semantic_provider_.koid(), kRootNodeId);

  // Set Prevous Node result.
  factory_ptr_->semantic_tree()->SetPreviousNode(nullptr);

  a11y::SwipeAction previous_action(&action_context_, screen_reader_context_.get(),
                                    kPreviousAction);
  a11y::SwipeAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();

  // Call PreviousAction Run().
  previous_action.Run(action_data);
  RunLoopUntilIdle();

  EXPECT_TRUE(factory_ptr_->semantic_tree()->IsGetPreviousNodeCalled());
  EXPECT_EQ(kRootNodeId, factory_ptr_->semantic_tree()->PreviousNodeCalledOnId());
  EXPECT_FALSE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());
  EXPECT_FALSE(mock_tts_engine_.ReceivedCancel());
  EXPECT_FALSE(mock_tts_engine_.ReceivedSpeak());
}

// When SetA11yFocus fails then SwipeAction should not call TTS to speak.
TEST_F(SwipeActionTest, SetA11yFocusFailed) {
  uint32_t node_id = 0;
  AddNodeToSemanticTree();

  // Update focused node.
  a11y_focus_manager_ptr_->UpdateA11yFocus(semantic_provider_.koid(), node_id);

  // Update SetA11yFocus() callback status to fail.
  a11y_focus_manager_ptr_->set_should_set_a11y_focus_fail(true);

  // Set Next Node result.
  uint32_t next_node_id = kChildNodeId;
  std::string next_node_label = kChildNodeLabel;
  Node next_node = CreateTestNode(next_node_id, next_node_label);
  factory_ptr_->semantic_tree()->SetNextNode(&next_node);

  a11y::SwipeAction next_action(&action_context_, screen_reader_context_.get(), kNextAction);
  a11y::SwipeAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();

  // Call NextAction Run().
  next_action.Run(action_data);
  RunLoopUntilIdle();

  EXPECT_TRUE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());
  EXPECT_FALSE(mock_tts_engine_.ReceivedCancel());
  EXPECT_EQ(kRootNodeId, a11y_focus_manager_ptr_->GetA11yFocus().value().node_id);
  EXPECT_EQ(semantic_provider_.koid(),
            a11y_focus_manager_ptr_->GetA11yFocus().value().view_ref_koid);
  EXPECT_FALSE(mock_tts_engine_.ReceivedSpeak());
}

// NextAction should get focused node information and then call GetNextNode() to get the next node.
// Next action should then set focus to the new node and then read the label of the new node in
// focus using tts.
TEST_F(SwipeActionTest, NextActionPerformed) {
  AddNodeToSemanticTree();

  // Update focused node.
  a11y_focus_manager_ptr_->UpdateA11yFocus(semantic_provider_.koid(), kRootNodeId);

  // Set Next Node result.
  uint32_t next_node_id = kChildNodeId;
  std::string next_node_label = kChildNodeLabel;
  Node next_node = CreateTestNode(next_node_id, next_node_label);
  factory_ptr_->semantic_tree()->SetNextNode(&next_node);

  a11y::SwipeAction next_action(&action_context_, screen_reader_context_.get(), kNextAction);
  a11y::SwipeAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();

  // Call NextAction Run().
  next_action.Run(action_data);
  RunLoopUntilIdle();

  ASSERT_EQ(semantic_provider_.GetRequestedAction(),
            fuchsia::accessibility::semantics::Action::SHOW_ON_SCREEN);
  ASSERT_TRUE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());
  EXPECT_TRUE(mock_tts_engine_.ReceivedCancel());
  EXPECT_EQ(next_node_id, a11y_focus_manager_ptr_->GetA11yFocus().value().node_id);
  EXPECT_EQ(semantic_provider_.koid(),
            a11y_focus_manager_ptr_->GetA11yFocus().value().view_ref_koid);
  EXPECT_TRUE(mock_tts_engine_.ReceivedSpeak());

  // Check if Utterance and Speak functions are called in Tts.
  ASSERT_EQ(mock_tts_engine_.ExamineUtterances().size(), 1u);
  EXPECT_EQ(mock_tts_engine_.ExamineUtterances()[0].message(), next_node_label);
}

// Previous action should get focused node information and then call GetPrevousNode() to get the
// previous node. Previous action should then set focus to the new node and then read the label of
// the new node in focus using tts.
TEST_F(SwipeActionTest, PreviousActionPerformed) {
  AddNodeToSemanticTree();

  // Update focused node.
  a11y_focus_manager_ptr_->UpdateA11yFocus(semantic_provider_.koid(), kRootNodeId);

  // Set Previous Node result.
  uint32_t previous_node_id = kChildNodeId;
  std::string previous_node_label = kChildNodeLabel;
  Node previous_node = CreateTestNode(previous_node_id, previous_node_label);
  factory_ptr_->semantic_tree()->SetPreviousNode(&previous_node);

  a11y::SwipeAction previous_action(&action_context_, screen_reader_context_.get(),
                                    kPreviousAction);
  a11y::SwipeAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();

  // Call PreviousAction Run().
  previous_action.Run(action_data);
  RunLoopUntilIdle();

  ASSERT_EQ(semantic_provider_.GetRequestedAction(),
            fuchsia::accessibility::semantics::Action::SHOW_ON_SCREEN);
  ASSERT_TRUE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());
  EXPECT_TRUE(mock_tts_engine_.ReceivedCancel());
  EXPECT_EQ(previous_node_id, a11y_focus_manager_ptr_->GetA11yFocus().value().node_id);
  EXPECT_EQ(semantic_provider_.koid(),
            a11y_focus_manager_ptr_->GetA11yFocus().value().view_ref_koid);
  EXPECT_TRUE(mock_tts_engine_.ReceivedSpeak());

  // Check if Utterance and Speak functions are called in Tts.
  ASSERT_EQ(mock_tts_engine_.ExamineUtterances().size(), 1u);
  EXPECT_EQ(mock_tts_engine_.ExamineUtterances()[0].message(), previous_node_label);
}

}  // namespace
}  // namespace accessibility_test
