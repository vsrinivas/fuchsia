// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fuchsia/ui/input/cpp/fidl.h>
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

// This test exercises the coordinate transform logic applied to pointer events
// sent to each client. We set up a scene with two translated but overlapping
// Views, and see if events are conveyed to the client in an appropriate way.
//
// The geometry is constrained to a 9x9 display and layer, with two 5x5
// rectangles that intersect in one pixel, like so:
//
//     x 1 1 1 1 - - - -
//     1 1 1 1 1 - - - -
//     1 1 1 1 1 - U - -
//     1 1 1 1 1 M - - -
//     1 1 1 1 y 2 2 2 2
//     - - - - 2 2 2 2 2      x - View 1 origin
//     - - - - 2 2 2 2 2      y - View 2 origin
//     - - - - 2 2 2 2 2      M - move event
//     - - - - 2 2 2 2 2      U - up event
//
// To create this test setup, we perform translation of each View itself (i.e.,
// (0,0) and (4,4)), in addition to aligning (translating) each View's Shape to
// its owning View.
//
// View 1 creates its rectangle in the upper left quadrant; the View's origin
// is marked 'x'. Similarly, View 2 creates its rectangle in the bottom right
// quadrant; the View's origin marked 'y'.
//
// The hit test occurs at the center of the screen (colocated with View 2's
// origin at 'y'), at (4,4) in device space. The touch events move diagonally up
// and to the right, and we have the following correspondence of coordinates:
//
// Event  Mark  Device  View-1  View-2
// ADD    y     (4,4)   (4,4)   (0, 0)
// DOWN   y     (4,4)   (4,4)   (0, 0)
// MOVE   M     (5,3)   (5,3)   (1,-1)
// UP     U     (6,2)   (6,2)   (2,-2)
// REMOVE U     (6,2)   (6,2)   (2,-2)
//
// N.B. View 1 sits *above* View 2 in elevation; hence, View 1 should receive
// the focus event.
//
// N.B. This test is carefully constructed to avoid Vulkan functionality.

namespace lib_ui_input_tests {

using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;

// Class fixture for TEST_F. Sets up a 9x9 "display" for GfxSystem.
class CoordinateTransformTest : public InputSystemTest {
 protected:
  uint32_t test_display_width_px() const override { return 9; }
  uint32_t test_display_height_px() const override { return 9; }
};

TEST_F(CoordinateTransformTest, CoordinateTransform) {
  SessionWrapper presenter(scenic());

  zx::eventpair vh1, v1, vh2, v2;
  CreateTokenPair(&vh1, &v1);
  CreateTokenPair(&vh2, &v2);

  // Tie the test's dispatcher clock to the system (real) clock.
  RunLoopUntil(zx::clock::get_monotonic());

  // "Presenter" sets up a scene with two ViewHolders.
  uint32_t compositor_id = 0;
  presenter.RunNow(
      [this, &compositor_id, vh1 = std::move(vh1), vh2 = std::move(vh2)](
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

        // Add local root node to the scene. Attach two entity nodes that
        // perform translation for the two clients; attach ViewHolders.
        scene.AddChild(*root_node);
        scenic::EntityNode translate_1(session), translate_2(session);
        scenic::ViewHolder holder_1(session, std::move(vh1), "holder_1"),
            holder_2(session, std::move(vh2), "holder_2");

        root_node->AddChild(translate_1);
        translate_1.SetTranslation(0, 0, -2);
        translate_1.Attach(holder_1);

        root_node->AddChild(translate_2);
        translate_2.SetTranslation(4, 4, -1);
        translate_2.Attach(holder_2);

        RequestToPresent(session);
      });

  // Client 1 vends a View to the global scene.
  SessionWrapper client_1(scenic());
  client_1.RunNow(
      [this, v1 = std::move(v1)](scenic::Session* session,
                                 scenic::EntityNode* root_node) mutable {
        scenic::View view_1(session, std::move(v1), "view_1");
        view_1.AddChild(*root_node);

        scenic::ShapeNode shape(session);
        shape.SetTranslation(2, 2, 0);  // Center the shape within the View.
        root_node->AddPart(shape);

        scenic::Rectangle rec(session, 5, 5);  // Simple; no real GPU work.
        shape.SetShape(rec);

        scenic::Material material(session);
        shape.SetMaterial(material);

        RequestToPresent(session);
      });

  // Client 2 vends a View to the global scene.
  SessionWrapper client_2(scenic());
  client_2.RunNow(
      [this, v2 = std::move(v2)](scenic::Session* session,
                                 scenic::EntityNode* root_node) mutable {
        scenic::View view_2(session, std::move(v2), "view_2");
        view_2.AddChild(*root_node);

        scenic::ShapeNode shape(session);
        shape.SetTranslation(2, 2, 0);  // Center the shape within the View.
        root_node->AddPart(shape);

        scenic::Rectangle rec(session, 5, 5);  // Simple; no real GPU work.
        shape.SetShape(rec);

        scenic::Material material(session);
        shape.SetMaterial(material);

        RequestToPresent(session);
      });

  // Multi-agent scene is now set up, send in the input.
  presenter.RunNow([this, compositor_id](scenic::Session* session,
                                         scenic::EntityNode* root_node) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts in the direct center of the 9x9 display.
    // The sequence ends 2x2 diagonally away (north-east) from the touch down.
    session->Enqueue(pointer.Add(4, 4));
    session->Enqueue(pointer.Down(4, 4));
    session->Enqueue(pointer.Move(5, 3));
    session->Enqueue(pointer.Up(6, 2));
    session->Enqueue(pointer.Remove(6, 2));
    RunLoopUntilIdle();

#if 0
    FXL_LOG(INFO) << DumpScenes();  // Handy debugging.
#endif
  });

  client_1.ExamineEvents([this](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 6u) << "Should receive exactly 6 input events.";

    // ADD
    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 4, 4));

    // FOCUS
    EXPECT_TRUE(events[1].is_focus());
    EXPECT_TRUE(events[1].focus().focused);

    // DOWN
    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[2].pointer(), 1u, PointerEventPhase::DOWN, 4, 4));

    // MOVE
    EXPECT_TRUE(events[3].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[3].pointer(), 1u, PointerEventPhase::MOVE, 5, 3));

    // UP
    EXPECT_TRUE(events[4].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[4].pointer(), 1u, PointerEventPhase::UP, 6, 2));

    // REMOVE
    EXPECT_TRUE(events[5].is_pointer());
    EXPECT_TRUE(PointerMatches(events[5].pointer(), 1u,
                               PointerEventPhase::REMOVE, 6, 2));
  });

  client_2.ExamineEvents([this](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 5u) << "Should receive exactly 5 input events.";

    // ADD
    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 0, 0));

    // DOWN
    EXPECT_TRUE(events[1].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[1].pointer(), 1u, PointerEventPhase::DOWN, 0, 0));

    // MOVE
    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(PointerMatches(events[2].pointer(), 1u, PointerEventPhase::MOVE,
                               1, -1));

    // UP
    EXPECT_TRUE(events[3].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[3].pointer(), 1u, PointerEventPhase::UP, 2, -2));

    // REMOVE
    EXPECT_TRUE(events[4].is_pointer());
    EXPECT_TRUE(PointerMatches(events[4].pointer(), 1u,
                               PointerEventPhase::REMOVE, 2, -2));

#if 0
    for (const auto& event : events)
      FXL_LOG(INFO) << "Client got: " << event;  // Handy debugging.
#endif
  });
}

}  // namespace lib_ui_input_tests
