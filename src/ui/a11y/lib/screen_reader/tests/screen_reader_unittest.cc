// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_manager.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_drag_recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_n_tap_recognizer.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_tts_engine.h"
#include "src/ui/a11y/lib/semantics/semantics_manager.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/testing/input.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"
#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {
namespace {

using fuchsia::accessibility::semantics::SemanticsManager;
using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using PointerEventPhase = fuchsia::ui::input::PointerEventPhase;

using fuchsia::accessibility::semantics::Attributes;
using fuchsia::accessibility::semantics::Hit;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::Role;
using fuchsia::accessibility::semantics::SemanticsManager;

const std::string kSemanticTreeSingle = "Node_id: 0, Label:Label A";
constexpr int kMaxLogBufferSize = 1024;

class ScreenReaderTest : public gtest::TestLoopFixture {
 public:
  ScreenReaderTest()
      : tts_manager_(context_provider_.context()),
        semantics_manager_(std::make_unique<a11y::SemanticTreeServiceFactory>(),
                           context_provider_.context()->outgoing()->debug_dir()),
        screen_reader_(&semantics_manager_, &tts_manager_),
        semantic_provider_(&semantics_manager_) {
    screen_reader_.BindGestures(gesture_manager_.gesture_handler());
  }

  void SendPointerEvents(const std::vector<PointerParams> &events) {
    for (const auto &event : events) {
      gesture_manager_.OnEvent(ToPointerEvent(event, 0 /*event time (unused)*/,
                                              a11y::GetKoid(semantic_provider_.view_ref())));
    }
  }

  void CreateOnOneFingerTapAction() {
    SendPointerEvents(
        TapEvents(1, {0, 0} /*global coordinates of tap ignored by mock semantic provider*/));
  }

  sys::testing::ComponentContextProvider context_provider_;
  a11y::TtsManager tts_manager_;
  a11y::SemanticsManager semantics_manager_;
  a11y::GestureManager gesture_manager_;
  a11y::ScreenReader screen_reader_;
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
  node.set_location(std::move(box));
  fuchsia::ui::gfx::mat4 transform;
  node.set_transform(std::move(transform));
  return node;
}

TEST_F(ScreenReaderTest, OnOneFingerSingleTapAction) {
  // Initialize Mock TTS Engine.
  accessibility_test::MockTtsEngine mock_tts_engine;
  fidl::InterfaceHandle<fuchsia::accessibility::tts::Engine> engine_handle =
      mock_tts_engine.GetHandle();
  tts_manager_.RegisterEngine(
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
  semantic_provider_.UpdateSemanticNodes(std::move(update_nodes));
  RunLoopUntilIdle();

  // Commit nodes.
  semantic_provider_.CommitUpdates();
  RunLoopUntilIdle();

  // Check that the committed node is present in the semantic tree.
  vfs::PseudoDir *debug_dir = context_provider_.context()->outgoing()->debug_dir();
  vfs::internal::Node *test_node;
  ASSERT_EQ(ZX_OK, debug_dir->Lookup(std::to_string(a11y::GetKoid(semantic_provider_.view_ref())),
                                     &test_node));
  char buffer[kMaxLogBufferSize];
  accessibility_test::ReadFile(test_node, kSemanticTreeSingle.size(), buffer);
  EXPECT_EQ(kSemanticTreeSingle, buffer);

  semantic_provider_.SetHitTestResult(0);

  // Create OnOneFingerTap Action.
  CreateOnOneFingerTapAction();
  RunLoopFor(a11y::OneFingerNTapRecognizer::kTapTimeout);

  // Verify that TTS is called when OneFingerTapAction was performed.
  EXPECT_TRUE(mock_tts_engine.ReceivedSpeak());
  // Check if Utterance and Speak functions are called in Tts.
  ASSERT_EQ(mock_tts_engine.ExamineUtterances().size(), 1u);
  EXPECT_EQ(mock_tts_engine.ExamineUtterances()[0].message(), "Label A");
}

TEST_F(ScreenReaderTest, OnOneFingerDoubleTapAction) {
  // Creating test node to update.
  std::vector<Node> update_nodes;
  Node node = CreateTestNode(0, "Label A");
  Node clone_node;
  node.Clone(&clone_node);
  update_nodes.push_back(std::move(clone_node));

  // Update the node created above.
  semantic_provider_.UpdateSemanticNodes(std::move(update_nodes));
  RunLoopUntilIdle();

  // Commit nodes.
  semantic_provider_.CommitUpdates();
  RunLoopUntilIdle();

  semantic_provider_.SetHitTestResult(0);

  semantic_provider_.SetRequestedAction(fuchsia::accessibility::semantics::Action::SET_FOCUS);

  // Create OnOneFingerDoubleTap Action.
  CreateOnOneFingerTapAction();
  CreateOnOneFingerTapAction();
  RunLoopFor(a11y::OneFingerNTapRecognizer::kTapTimeout);

  EXPECT_EQ(fuchsia::accessibility::semantics::Action::DEFAULT,
            semantic_provider_.GetRequestedAction());
}

TEST_F(ScreenReaderTest, OnOneFingerDragAction) {
  // Initialize Mock TTS Engine.
  accessibility_test::MockTtsEngine mock_tts_engine;
  fidl::InterfaceHandle<fuchsia::accessibility::tts::Engine> engine_handle =
      mock_tts_engine.GetHandle();
  tts_manager_.RegisterEngine(
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
  semantic_provider_.UpdateSemanticNodes(std::move(update_nodes));
  RunLoopUntilIdle();

  // Commit nodes.
  semantic_provider_.CommitUpdates();
  RunLoopUntilIdle();

  semantic_provider_.SetHitTestResult(0);

  // Create one finger drag action.
  glm::vec2 first_update_ndc_position = {0, .7f};
  auto first_update_local_coordinates = ToLocalCoordinates(first_update_ndc_position);

  SendPointerEvents(DownEvents(1, {}) + MoveEvents(1, {}, first_update_ndc_position));

  // Wait for the drag delay to elapse, at which point the recognizer should claim the win and
  // invoke the update callback.
  RunLoopFor(a11y::OneFingerDragRecognizer::kDefaultMinDragDuration);

  SendPointerEvents(MoveEvents(1, first_update_ndc_position, first_update_ndc_position, 1) +
                    UpEvents(1, first_update_ndc_position));

  RunLoopUntilIdle();

  // Verify that TTS is called when OneFingerTapAction was performed.
  EXPECT_TRUE(mock_tts_engine.ReceivedSpeak());
  // Check if Utterance and Speak functions are called in Tts.
  ASSERT_EQ(mock_tts_engine.ExamineUtterances().size(), 1u);
  EXPECT_EQ(mock_tts_engine.ExamineUtterances()[0].message(), "Label A");
}

}  // namespace
}  // namespace accessibility_test
