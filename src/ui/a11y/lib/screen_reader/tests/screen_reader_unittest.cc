// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_manager.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_tts_engine.h"
#include "src/ui/a11y/lib/semantics/semantics_manager.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
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

  AccessibilityPointerEvent GetDefaultPointerEvent();
  void CreateOnOneFingerTapAction();

  sys::testing::ComponentContextProvider context_provider_;
  a11y::TtsManager tts_manager_;
  a11y::SemanticsManager semantics_manager_;
  a11y::GestureManager gesture_manager_;
  a11y::ScreenReader screen_reader_;
  accessibility_test::MockSemanticProvider semantic_provider_;
};

// Returns a default Accessibility Pointer Event.
AccessibilityPointerEvent ScreenReaderTest::GetDefaultPointerEvent() {
  AccessibilityPointerEvent event;
  event.set_event_time(10);
  event.set_device_id(1);
  event.set_pointer_id(1);
  event.set_type(fuchsia::ui::input::PointerEventType::TOUCH);
  event.set_phase(PointerEventPhase::ADD);
  event.set_ndc_point({4, 4});
  event.set_viewref_koid(a11y::GetKoid(semantic_provider_.view_ref()));
  event.set_local_point({2, 2});
  return event;
}

void ScreenReaderTest::CreateOnOneFingerTapAction() {
  {
    auto event = GetDefaultPointerEvent();
    gesture_manager_.OnEvent(std::move(event));
  }
  {
    auto event = GetDefaultPointerEvent();
    event.set_phase(PointerEventPhase::DOWN);
    gesture_manager_.OnEvent(std::move(event));
  }
  {
    auto event = GetDefaultPointerEvent();
    event.set_phase(PointerEventPhase::UP);
    gesture_manager_.OnEvent(std::move(event));
  }
  {
    auto event = GetDefaultPointerEvent();
    event.set_phase(PointerEventPhase::REMOVE);
    gesture_manager_.OnEvent(std::move(event));
  }
}

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

TEST_F(ScreenReaderTest, OnOneFingerTapAction) {
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
  RunLoopUntilIdle();

  // Verify that TTS is called when OneFingerTapAction was performed.
  EXPECT_TRUE(mock_tts_engine.ReceivedSpeak());
  // Check if Utterance and Speak functions are called in Tts.
  ASSERT_EQ(mock_tts_engine.ExamineUtterances().size(), 1u);
  EXPECT_EQ(mock_tts_engine.ExamineUtterances()[0].message(), "Label A");
}

}  // namespace
}  // namespace accessibility_test
