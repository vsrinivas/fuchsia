// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/inject_pointer_event_action.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <zircon/types.h>

#include <memory>

#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/input_injection/tests/mocks/mock_injector_manager.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_screen_reader_context.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantics_source.h"
#include "src/ui/a11y/lib/semantics/util/semantic_transform.h"

namespace accessibility_test {
namespace {

using fuchsia::accessibility::semantics::Node;

class InjectPointerEventActionTest : public gtest::TestLoopFixture {
 public:
  InjectPointerEventActionTest() = default;
  ~InjectPointerEventActionTest() override = default;

  void SetUp() override {
    mock_semantic_provider_ = std::make_unique<MockSemanticProvider>(nullptr, nullptr);

    mock_semantics_source_ = std::make_unique<MockSemanticsSource>();
    mock_injector_manager_ = std::make_unique<MockInjectorManager>();

    action_context_.semantics_source = mock_semantics_source_.get();
    action_context_.injector_manager = mock_injector_manager_.get();

    screen_reader_context_ = std::make_unique<MockScreenReaderContext>();
  }

  std::unique_ptr<MockSemanticsSource> mock_semantics_source_;
  std::unique_ptr<MockInjectorManager> mock_injector_manager_;
  a11y::ScreenReaderAction::ActionContext action_context_;
  std::unique_ptr<MockScreenReaderContext> screen_reader_context_;
  std::unique_ptr<MockSemanticProvider> mock_semantic_provider_;
};

// Tests the case where the inject pointer event action is called for a valid node.
TEST_F(InjectPointerEventActionTest, InjectPointerEventAction) {
  // Create test node to target with injected input event.
  std::vector<Node> update_nodes;
  uint32_t node_id = 0;
  Node node = CreateTestNode(node_id, "Label A");
  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 2.0, .y = 4.0, .z = 6.0}};
  node.set_location(bounding_box);
  mock_semantics_source_->CreateSemanticNode(mock_semantic_provider_->koid(), std::move(node));

  // Create a non-trivial node->root semantic transform.
  a11y::SemanticTransform transform;
  fuchsia::ui::gfx::mat4 transform_mat;
  // Scale factors
  transform_mat.matrix[0] = 1.2;
  transform_mat.matrix[5] = 3.4;
  transform_mat.matrix[10] = 5.6;
  transform.ChainLocalTransform(transform_mat);
  mock_semantics_source_->SetNodeToRootTransform(transform);

  // Populate the gesture context.
  a11y::ScreenReaderContext* context = screen_reader_context_.get();
  a11y::InjectPointerEventAction inject_pointer_event_action(&action_context_, context);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider_->koid();
  gesture_context.last_event_time = 10;
  gesture_context.last_event_phase = fuchsia::ui::input::PointerEventPhase::MOVE;
  gesture_context.last_event_pointer_id = 1;
  gesture_context.starting_pointer_locations[1].local_point.x = 0.f;
  gesture_context.starting_pointer_locations[1].local_point.y = 0.f;
  gesture_context.current_pointer_locations[1].local_point.x = 1.f;
  gesture_context.current_pointer_locations[1].local_point.y = 1.f;

  // Update focused node.
  screen_reader_context_->mock_a11y_focus_manager_ptr()->SetA11yFocus(
      mock_semantic_provider_->koid(), node_id, [](bool result) { EXPECT_TRUE(result); });

  inject_pointer_event_action.Run(gesture_context);
  RunLoopUntilIdle();

  const auto& injected_events =
      mock_injector_manager_->GetEventsForKoid(mock_semantic_provider_->koid());
  ASSERT_EQ(injected_events.size(), 1u);
  const auto& pointer_event = injected_events[0].pointer();

  EXPECT_EQ(pointer_event.device_id, 1u);
  EXPECT_EQ(pointer_event.event_time, 10u);
  EXPECT_EQ(pointer_event.pointer_id, 1u);
  EXPECT_EQ(pointer_event.type, fuchsia::ui::input::PointerEventType::TOUCH);
  EXPECT_EQ(pointer_event.phase, fuchsia::ui::input::PointerEventPhase::MOVE);

  // The coordinates for the injected event are computed by translating the
  // center of the node's bounding box into root space, and then displacing that
  // point by the root-space displacement from the start to current pointer
  // location.
  // So, in this case, the center of the node's bounding box is at
  // (1, 2) in node-local coordinates. Applying the transform yields (1.2, 6.8)
  // in root space. The displacement in root space is (1, 1), which yields an
  // injected pointer event at (2.2, 7.8).
  EXPECT_GT(pointer_event.x, 2.19f);
  EXPECT_LT(pointer_event.x, 2.21f);
  EXPECT_GT(pointer_event.y, 7.79f);
  EXPECT_LT(pointer_event.y, 7.81f);
}

}  // namespace
}  // namespace accessibility_test
