// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async/cpp/time.h>
#include <lib/zx/eventpair.h>

#include "garnet/lib/ui/input/tests/util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/fxl/logging.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/ui/input/cpp/formatting.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"

// This test exercises the event delivery logic for mouse and touchpad events.
// The mouse moves from the bottom left corner to the upper right corner.  While
// the "down-move-up" sequence should be delivered to the focused client, the
// prefix and suffix "move" events are delivered to the top-level client without
// triggering a focus change.
//
// The geometry of the display and layer are contrained to a 7x7 square. Two 5x5
// views are overlayed on top; client 1 is higher than client 2 and receives the
// three prefix "move" events and "down-move-up" sequence. Client 2 receives the
// single suffix "move" event.
//
// We also have the presenter client add three ShapeNodes on top to emulate
// mouse cursor placement. To save the hassle of moving the cursor around, we
// simply make the ShapeNodes cover the entire screen. The expected behavior is
// to ignore these mouse cursors, because they do not have an owning View.
//
//     - - y 2 2 2 M
//     - - 2 2 2 U 2
//     x 1 1 1 M 2 2   x - client 1's view origin
//     1 1 1 D 1 2 2   y - client 2's view origin
//     1 1 M 1 1 2 2   M - mouse move
//     1 M 1 1 1 - -   D - mouse down
//     M 1 1 1 1 - -   U - mouse up
//
// To create this test setup, we perform translation of each View (i.e., (0,2)
// and (2, 0)), in addition to aligning (translating) each View's Shape to its
// owning View.
//
// We have the following correspondence of coordinates:
//
// Event   Mark  Device  View-1  View-2
// Move-1  M     (0,6)   (0, 4)  n/a
// Move-2  M     (1,5)   (1, 3)  n/a
// Move-3  M     (2,4)   (2, 2)  n/a
// Down    D     (3,3)   (3, 1)  n/a
// Move-4  M     (4,2)   (4, 0)  n/a
// Up      U     (5,1)   (5,-1)  n/a
// Move-5  M     (6,0)   n/a     (4,0)
//
// NOTE: This test is carefully constructed to avoid Vulkan functionality.

namespace lib_ui_input_tests {

using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;

// Class fixture for TEST_F. Sets up a 7x7 "display" for GfxSystem.
class MouseDeliveryTest : public InputSystemTest {
 protected:
  uint32_t test_display_width_px() const override { return 7; }
  uint32_t test_display_height_px() const override { return 7; }
};

namespace {
// Every client in this test file set up the same way.
void CreateClient(scenic::Session* session, zx::eventpair view_token,
                  scenic::EntityNode* root_node, std::string client_name,
                  InputSystemTest* test) {
  // Connect our root node to the presenter's node.
  scenic::View view(session, std::move(view_token), client_name);
  view.AddChild(*root_node);

  scenic::ShapeNode shape(session);
  shape.SetTranslation(2, 2, 0);  // Center the shape within the View.
  root_node->AddPart(shape);

  scenic::Rectangle rec(session, 5, 5);  // Simple; no real GPU work.
  shape.SetShape(rec);

  scenic::Material material(session);
  shape.SetMaterial(material);

  test->RequestToPresent(session);
}
}  // namespace

TEST_F(MouseDeliveryTest, StandardTest) {
  SessionWrapper presenter(scenic());

  zx::eventpair v1_token, vh1_token, v2_token, vh2_token;
  CreateTokenPair(&v1_token, &vh1_token);
  CreateTokenPair(&v2_token, &vh2_token);

  // Tie the test's dispatcher clock to the system (real) clock.
  RunLoopUntil(zx::clock::get_monotonic());

  // "Presenter" sets up a scene with two views.
  uint32_t compositor_id = 0;
  presenter.RunNow(
      [this, &compositor_id, vh1_token = std::move(vh1_token),
       vh2_token = std::move(vh2_token)](
          scenic::Session* session, scenic::EntityNode* root_node) mutable {
        // Minimal scene.
        scenic::Compositor compositor(session);
        compositor_id = compositor.id();

        scenic::Scene scene(session);
        scenic::Camera camera(scene);
        scenic::Renderer renderer(session);
        renderer.SetCamera(camera);

        scenic::Layer layer(session);
        layer.SetSize(test_display_width_px(), test_display_height_px());
        layer.SetRenderer(renderer);

        scenic::LayerStack layer_stack(session);
        layer_stack.AddLayer(layer);
        compositor.SetLayerStack(layer_stack);

        // Add local root node to the scene, attach the translated view holders.
        scene.AddChild(*root_node);
        scenic::EntityNode translate_1(session), translate_2(session);
        scenic::ViewHolder holder_1(session, std::move(vh1_token), "holder_1"),
            holder_2(session, std::move(vh2_token), "holder_2");

        root_node->AddChild(translate_1);
        translate_1.SetTranslation(0, 2, -2);
        translate_1.Attach(holder_1);

        root_node->AddChild(translate_2);
        translate_2.SetTranslation(2, 0, -1);
        translate_2.Attach(holder_2);

        // Add three "mouse cursors" to the scene.
        for (int i = 0; i < 3; ++i) {
          scenic::ShapeNode cursor(session);
          cursor.SetTranslation(3, 3, -100);
          cursor.SetLabel("mouse cursor");
          scene.AddChild(cursor);

          scenic::Rectangle rec(session, 7, 7);
          cursor.SetShape(rec);

          scenic::Material material(session);
          cursor.SetMaterial(material);
        }

        RequestToPresent(session);
      });

  // Client 1 sets up its content.
  SessionWrapper client_1(scenic());
  client_1.RunNow(
      [this, v1_token = std::move(v1_token)](
          scenic::Session* session, scenic::EntityNode* root_node) mutable {
        CreateClient(session, std::move(v1_token), root_node, "View 1", this);
      });

  // Client 2 sets up its content.
  SessionWrapper client_2(scenic());
  client_2.RunNow(
      [this, v2_token = std::move(v2_token)](
          scenic::Session* session, scenic::EntityNode* root_node) mutable {
        CreateClient(session, std::move(v2_token), root_node, "View 2", this);
      });

  // Scene is now set up, send in the input.
  presenter.RunNow([this, compositor_id](scenic::Session* session,
                                         scenic::EntityNode* root_node) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::MOUSE);
    // A touch sequence that starts at the (0,6) location of the 7x7 display and
    // ends in the (6,0) location. Sent in as device (display) coordinates.
    session->Enqueue(pointer.Move(0, 6));
    session->Enqueue(pointer.Move(1, 5));
    session->Enqueue(pointer.Move(2, 4));
    session->Enqueue(pointer.Down(3, 3));
    session->Enqueue(pointer.Move(4, 2));
    session->Enqueue(pointer.Up(5, 1));
    session->Enqueue(pointer.Move(6, 0));

    RunLoopUntilIdle();
#if 0
    FXL_LOG(INFO) << DumpScenes();  // Handy debugging.
#endif
  });

  // Verify client 1's inputs have mouse events.
  client_1.ExamineEvents([this](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 7u) << "Should receive exactly 7 input events.";

    // MOVE
    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[0].pointer(), 1u, PointerEventPhase::MOVE, 0, 4));

    // MOVE
    EXPECT_TRUE(events[1].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[1].pointer(), 1u, PointerEventPhase::MOVE, 1, 3));

    // MOVE
    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[2].pointer(), 1u, PointerEventPhase::MOVE, 2, 2));

    // FOCUS
    EXPECT_TRUE(events[3].is_focus());
    EXPECT_TRUE(events[3].focus().focused);

    // DOWN
    EXPECT_TRUE(events[4].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[4].pointer(), 1u, PointerEventPhase::DOWN, 3, 1));

    // MOVE
    EXPECT_TRUE(events[5].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[5].pointer(), 1u, PointerEventPhase::MOVE, 4, 0));

    // UP
    EXPECT_TRUE(events[6].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[6].pointer(), 1u, PointerEventPhase::UP, 5, -1));
  });

  // Verify client 2's input has one mouse event.
  client_2.ExamineEvents([this](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 1u) << "Should receive exactly 1 event.";

    // MOVE
    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[0].pointer(), 1u, PointerEventPhase::MOVE, 4, 0));
  });
}

TEST_F(MouseDeliveryTest, NoFocusTest) {
  SessionWrapper presenter(scenic());

  zx::eventpair v1_token, vh1_token, v2_token, vh2_token;
  CreateTokenPair(&v1_token, &vh1_token);
  CreateTokenPair(&v2_token, &vh2_token);

  // Tie the test's dispatcher clock to the system (real) clock.
  RunLoopUntil(zx::clock::get_monotonic());

  // "Presenter" sets up a scene with two views.
  uint32_t compositor_id = 0;
  presenter.RunNow(
      [this, &compositor_id, vh1_token = std::move(vh1_token),
       vh2_token = std::move(vh2_token)](
          scenic::Session* session, scenic::EntityNode* root_node) mutable {
        // Minimal scene.
        scenic::Compositor compositor(session);
        compositor_id = compositor.id();

        scenic::Scene scene(session);
        scenic::Camera camera(scene);
        scenic::Renderer renderer(session);
        renderer.SetCamera(camera);

        scenic::Layer layer(session);
        layer.SetSize(test_display_width_px(), test_display_height_px());
        layer.SetRenderer(renderer);

        scenic::LayerStack layer_stack(session);
        layer_stack.AddLayer(layer);
        compositor.SetLayerStack(layer_stack);

        // Add local root node to the scene, attach the translated view holders.
        scene.AddChild(*root_node);
        scenic::EntityNode translate_1(session), translate_2(session);
        scenic::ViewHolder holder_1(session, std::move(vh1_token), "holder_1"),
            holder_2(session, std::move(vh2_token), "holder_2");

        root_node->AddChild(translate_1);
        translate_1.SetTranslation(0, 2, -2);
        translate_1.Attach(holder_1);

        // Set view 1 to "no-focus".
        {
          fuchsia::ui::gfx::ViewProperties properties;
          properties.focus_change = false;
          holder_1.SetViewProperties(std::move(properties));
        }

        root_node->AddChild(translate_2);
        translate_2.SetTranslation(2, 0, -1);
        translate_2.Attach(holder_2);

        // Add three "mouse cursors" to the scene.
        for (int i = 0; i < 3; ++i) {
          scenic::ShapeNode cursor(session);
          cursor.SetTranslation(3, 3, -100);
          cursor.SetLabel("mouse cursor");
          scene.AddChild(cursor);

          scenic::Rectangle rec(session, 7, 7);
          cursor.SetShape(rec);

          scenic::Material material(session);
          cursor.SetMaterial(material);
        }

        RequestToPresent(session);
      });

  // Client 1 sets up its content.
  SessionWrapper client_1(scenic());
  client_1.RunNow(
      [this, v1_token = std::move(v1_token)](
          scenic::Session* session, scenic::EntityNode* root_node) mutable {
        CreateClient(session, std::move(v1_token), root_node, "View 1", this);
      });

  // Client 2 sets up its content.
  SessionWrapper client_2(scenic());
  client_2.RunNow(
      [this, v2_token = std::move(v2_token)](
          scenic::Session* session, scenic::EntityNode* root_node) mutable {
        CreateClient(session, std::move(v2_token), root_node, "View 2", this);
      });

  // Scene is now set up, send in the input.
  presenter.RunNow([this, compositor_id](scenic::Session* session,
                                         scenic::EntityNode* root_node) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::MOUSE);
    // A touch sequence that starts at the (0,6) location of the 7x7 display and
    // ends in the (6,0) location. Sent in as device (display) coordinates.
    session->Enqueue(pointer.Move(0, 6));
    session->Enqueue(pointer.Move(1, 5));
    session->Enqueue(pointer.Move(2, 4));
    session->Enqueue(pointer.Down(3, 3));
    session->Enqueue(pointer.Move(4, 2));
    session->Enqueue(pointer.Up(5, 1));
    session->Enqueue(pointer.Move(6, 0));

    RunLoopUntilIdle();
#if 0
    FXL_LOG(INFO) << DumpScenes();  // Handy debugging.
#endif
  });

  // Verify client 1's inputs have mouse events.
  client_1.ExamineEvents([this](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 6u) << "Should receive exactly 6 input events.";

    // MOVE
    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[0].pointer(), 1u, PointerEventPhase::MOVE, 0, 4));

    // MOVE
    EXPECT_TRUE(events[1].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[1].pointer(), 1u, PointerEventPhase::MOVE, 1, 3));

    // MOVE
    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[2].pointer(), 1u, PointerEventPhase::MOVE, 2, 2));

    // DOWN
    EXPECT_TRUE(events[3].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[3].pointer(), 1u, PointerEventPhase::DOWN, 3, 1));

    // MOVE
    EXPECT_TRUE(events[4].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[4].pointer(), 1u, PointerEventPhase::MOVE, 4, 0));

    // UP
    EXPECT_TRUE(events[5].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[5].pointer(), 1u, PointerEventPhase::UP, 5, -1));
  });

  // Verify client 2's input has one mouse event.
  client_2.ExamineEvents([this](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 1u) << "Should receive exactly 1 event.";

    // MOVE
    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[0].pointer(), 1u, PointerEventPhase::MOVE, 4, 0));
  });
}

}  // namespace lib_ui_input_tests
