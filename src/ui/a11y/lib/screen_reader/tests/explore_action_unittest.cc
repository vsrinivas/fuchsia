// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/explore_action.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <src/ui/a11y/lib/screen_reader/tests/mocks/mock_semantic_listener.h>
#include <src/ui/a11y/lib/screen_reader/tests/mocks/mock_tts_engine.h>

#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"
#include "src/ui/a11y/lib/util/util.h"

namespace accesibility_test {
namespace {

using fuchsia::accessibility::semantics::Attributes;
using fuchsia::accessibility::semantics::Hit;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::Role;
using fuchsia::accessibility::semantics::SemanticsManager;

const std::string kSemanticTreeSingle = "Node_id: 0, Label:Label A";
constexpr int kMaxLogBufferSize = 1024;

class ExploreActionTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    RealLoopFixture::SetUp();

    // Initialize ActionContext.
    action_context_.semantics_manager = &semantics_manager_;
    // Enabled Semantics Manager.
    action_context_.semantics_manager->SetSemanticsManagerEnabled(true);

    // Intiailize TtsManager.
    tts_manager_ = std::make_unique<a11y::TtsManager>(context_provider_.context());
    tts_manager_->OpenEngine(action_context_.tts_engine_ptr.NewRequest(),
                             [](fuchsia::accessibility::tts::TtsManager_OpenEngine_Result result) {
                               EXPECT_TRUE(result.is_response());
                             });
    RunLoopUntilIdle();

    // Create ViewRef eventpair.
    zx::eventpair a, b;
    zx::eventpair::create(0u, &a, &b);
    view_ref_ = fuchsia::ui::views::ViewRef({
        .reference = std::move(a),
    });

    action_context_.semantics_manager->SetDebugDirectory(
        context_provider_.context()->outgoing()->debug_dir());

    // Add Semantics Manager service.
    context_provider_.service_directory_provider()->AddService<SemanticsManager>(
        [this](fidl::InterfaceRequest<SemanticsManager> request) {
          action_context_.semantics_manager->AddBinding(std::move(request));
        });
    RunLoopUntilIdle();

    fuchsia::ui::views::ViewRef view_ref_connection;
    fidl::Clone(view_ref_, &view_ref_connection);
    semantic_listener_ = std::make_unique<accessibility_test::MockSemanticListener>(
        context_provider_.context(), std::move(view_ref_connection));
    RunLoopUntilIdle();
  }

  a11y::ScreenReaderAction::ActionContext action_context_;
  sys::testing::ComponentContextProvider context_provider_;
  fuchsia::ui::views::ViewRef view_ref_;
  std::unique_ptr<a11y::TtsManager> tts_manager_;
  std::unique_ptr<accessibility_test::MockSemanticListener> semantic_listener_;
  a11y::SemanticsManager semantics_manager_;
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
  node.set_location(std::move(box));
  fuchsia::ui::gfx::mat4 transform;
  node.set_transform(std::move(transform));
  return node;
}

TEST_F(ExploreActionTest, ReadLabel) {
  accessibility_test::MockTtsEngine mock_tts_engine;
  fidl::InterfaceHandle<fuchsia::accessibility::tts::Engine> engine_handle =
      mock_tts_engine.GetHandle();
  tts_manager_->RegisterEngine(
      std::move(engine_handle),
      [](fuchsia::accessibility::tts::EngineRegistry_RegisterEngine_Result result) {
        EXPECT_TRUE(result.is_response());
      });
  RunLoopUntilIdle();

  // Creating test node to update.
  std::vector<Node> update_nodes;
  Node node = CreateTestNode(0, "Label A");
  Node clone_node;
  node.Clone(&clone_node);
  update_nodes.push_back(std::move(clone_node));

  // Update the node created above.
  semantic_listener_->UpdateSemanticNodes(std::move(update_nodes));
  RunLoopUntilIdle();

  // Commit nodes.
  semantic_listener_->Commit();
  RunLoopUntilIdle();

  // Check that the committed node is present in the semantic tree.
  vfs::PseudoDir *debug_dir = context_provider_.context()->outgoing()->debug_dir();
  vfs::internal::Node *test_node;
  ASSERT_EQ(ZX_OK, debug_dir->Lookup(std::to_string(a11y::GetKoid(view_ref_)), &test_node));
  char buffer[kMaxLogBufferSize];
  accessibility_test::ReadFile(test_node, kSemanticTreeSingle.size(), buffer);
  EXPECT_EQ(kSemanticTreeSingle, buffer);

  a11y::ExploreAction explore_action(&action_context_);
  a11y::ExploreAction::ActionData action_data;
  action_data.koid = a11y::GetKoid(view_ref_);

  Hit hit;
  hit.set_node_id(0);
  semantic_listener_->SetHitTestingResult(&hit);

  // Call ExploreAction Run()
  explore_action.Run(action_data);
  RunLoopUntil([&mock_tts_engine] { return mock_tts_engine.ReceivedSpeak(); });

  // Check if Utterance and Speak functions are called in Tts.
  ASSERT_EQ(mock_tts_engine.ExamineUtterances().size(), 1u);
  EXPECT_EQ(mock_tts_engine.ExamineUtterances()[0].message(), "Label A");
}

}  // namespace
}  // namespace accesibility_test
