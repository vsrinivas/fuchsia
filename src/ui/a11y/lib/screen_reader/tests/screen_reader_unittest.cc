// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader.h"

#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_semantic_listener.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_tts_engine.h"
#include "src/ui/a11y/lib/semantics/semantics_manager.h"
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

class ScreenReaderTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    RealLoopFixture::SetUp();

    tts_manager_ = std::make_unique<a11y::TtsManager>(context_provider_.context());

    // Create ViewRef eventpair.
    zx::eventpair a, b;
    zx::eventpair::create(0u, &a, &b);
    view_ref_ = fuchsia::ui::views::ViewRef({
        .reference = std::move(a),
    });

    // Set Semantics Manager Debug Directory.
    semantics_manager_.SetDebugDirectory(context_provider_.context()->outgoing()->debug_dir());

    // Add Semantics Manager service.
    context_provider_.service_directory_provider()->AddService<SemanticsManager>(
        [this](fidl::InterfaceRequest<SemanticsManager> request) {
          semantics_manager_.AddBinding(std::move(request));
        });
    RunLoopUntilIdle();

    fuchsia::ui::views::ViewRef view_ref_connection;
    fidl::Clone(view_ref_, &view_ref_connection);
    semantic_listener_ = std::make_unique<accessibility_test::MockSemanticListener>(
        context_provider_.context(), std::move(view_ref_connection));
    RunLoopUntilIdle();

    // Initialize Screen Reader.
    screen_reader_ = std::make_unique<a11y::ScreenReader>(&semantics_manager_, tts_manager_.get(),
                                                          &gesture_manager_);
  }

  AccessibilityPointerEvent GetDefaultPointerEvent();
  void CreateOnOneFngerTapAction();

  std::unique_ptr<a11y::TtsManager> tts_manager_;
  a11y::SemanticsManager semantics_manager_;
  a11y::GestureManager gesture_manager_;
  std::unique_ptr<accessibility_test::MockSemanticListener> semantic_listener_;
  sys::testing::ComponentContextProvider context_provider_;
  fuchsia::ui::views::ViewRef view_ref_;
  std::unique_ptr<a11y::ScreenReader> screen_reader_;
};

// Returns a default Accessibility Pointer Event.
AccessibilityPointerEvent ScreenReaderTest::GetDefaultPointerEvent() {
  AccessibilityPointerEvent event;
  event.set_event_time(10);
  event.set_device_id(1);
  event.set_pointer_id(1);
  event.set_type(fuchsia::ui::input::PointerEventType::TOUCH);
  event.set_phase(PointerEventPhase::ADD);
  event.set_global_point({4, 4});
  event.set_viewref_koid(a11y::GetKoid(view_ref_));
  event.set_local_point({2, 2});
  return event;
}

void ScreenReaderTest::CreateOnOneFngerTapAction() {
  {
    // Down event.
    auto event = GetDefaultPointerEvent();
    event.set_phase(PointerEventPhase::DOWN);
    auto listener_callback = [](uint32_t device_id, uint32_t pointer_id,
                                fuchsia::ui::input::accessibility::EventHandling handled) {};
    gesture_manager_.OnEvent(std::move(event), std::move(listener_callback));
  }
  {
    // Send UP event.
    auto event = GetDefaultPointerEvent();
    event.set_phase(PointerEventPhase::UP);
    auto listener_callback = [](uint32_t device_id, uint32_t pointer_id,
                                fuchsia::ui::input::accessibility::EventHandling handled) {};
    gesture_manager_.OnEvent(std::move(event), std::move(listener_callback));
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
  // Intiailize Mock TTS Engine.
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

  Hit hit;
  hit.set_node_id(0);
  semantic_listener_->SetHitTestingResult(&hit);

  // Create OnOneFingerTap Action.
  CreateOnOneFngerTapAction();
  RunLoopUntilIdle();

  // Verify that TTS is called when OneFingerTapAction was performed.
  RunLoopUntil([&mock_tts_engine] { return mock_tts_engine.ReceivedSpeak(); });
  // Check if Utterance and Speak functions are called in Tts.
  ASSERT_EQ(mock_tts_engine.ExamineUtterances().size(), 1u);
  EXPECT_EQ(mock_tts_engine.ExamineUtterances()[0].message(), "Label A");
}

}  // namespace
}  // namespace accessibility_test
