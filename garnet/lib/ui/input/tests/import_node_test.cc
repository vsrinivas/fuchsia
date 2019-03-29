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

// This test exercises the event delivery logic for Views created with an
// ImportNode (v1), as opposed to a View resource (v2).
//
// The client has its root node attached to an ImportNode, which in turn is
// attached to an EntityNode in ViewManager. Here, we merely simulate the
// structure of such a graph; we rely on the invariant that in the ViewManager
// world, each client's View terminates in an ImportNode before transitioning to
// a ViewManager node.
//
//
// The geometry is contrained to a 7x7 display and layer, with one 5x5 rectangle
// that sits at an offset, like so:
//
//     - - - - - - -
//     - - - - - - -
//     - - x 1 1 1 U
//     - - 1 1 1 M 1    x - client's view origin
//     - - 1 1 D 1 1    D - add and down events
//     - - 1 1 1 1 1    M - move event
//     - - 1 1 1 1 1    U - up and remove events
//
// To create this test setup, we perform translation of the View itself (i.e.,
// (2,2)), in addition to aligning (translating) the Shape to its View.
//
// The touch events have the following correspondence of coordinates:
//
// Event   Mark  Device  View
// ADD     D     (4,4)   (2,2)
// DOWN    D     (4,4)   (2,2)
// MOVE    M     (5,3)   (3,1)
// UP      U     (6,2)   (4,0)
// REMOVE  U     (6,2)   (4,0)
//
// N.B. This test is carefully constructed to avoid Vulkan functionality.

namespace lib_ui_input_tests {

using ScenicEvent = fuchsia::ui::scenic::Event;
using escher::impl::CommandBufferSequencer;
using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;
using fuchsia::ui::scenic::SessionListener;
using scenic_impl::Scenic;
using scenic_impl::gfx::Display;
using scenic_impl::gfx::DisplayManager;
using scenic_impl::gfx::test::GfxSystemForTest;
using scenic_impl::input::InputSystem;
using scenic_impl::test::ScenicTest;

// Class fixture for TEST_F. Sets up a 7x7 "display" for GfxSystem.
class ImportNodeTest : public InputSystemTest {
 protected:
  uint32_t test_display_width_px() const override { return 7; }
  uint32_t test_display_height_px() const override { return 7; }
};

TEST_F(ImportNodeTest, ImportNodeEventDelivery) {
  SessionWrapper presenter(scenic());

  zx::eventpair import_token, export_token;
  CreateTokenPair(&import_token, &export_token);

  // Tie the test's dispatcher clock to the system (real) clock.
  RunLoopUntil(zx::clock::get_monotonic());

  // "Presenter" sets up a scene with one view.
  uint32_t compositor_id = 0;
  presenter.RunNow(
      [this, &compositor_id, export_token = std::move(export_token)](
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

        // Add local root node to the scene. Add a per-view translation node;
        // export that node so that the client can hang their content from it.
        scene.AddChild(*root_node);
        scenic::EntityNode translate_child_view(session);
        translate_child_view.SetTranslation(2, 2, 0);
        translate_child_view.SetTag(1);  // Emulate ViewManager's usage.
        root_node->AddChild(translate_child_view);
        translate_child_view.Export(std::move(export_token));

        RequestToPresent(session);
      });

  // Client sets up its content.
  SessionWrapper client(scenic());
  client.RunNow(
      [this, import_token = std::move(import_token)](
          scenic::Session* session, scenic::EntityNode* root_node) mutable {
        // Connect our root node to the presenter's root node.
        // We've "imported" the presenter's root node in our session.
        scenic::ImportNode import(session);
        import.Bind(std::move(import_token));
        import.AddChild(*root_node);

        scenic::ShapeNode shape(session);
        shape.SetTranslation(2, 2, 0);  // Center the shape within the View.
        root_node->AddPart(shape);

        scenic::Rectangle rec(session, 5, 5);  // Simple; no real GPU work.
        shape.SetShape(rec);

        scenic::Material material(session);
        shape.SetMaterial(material);

        RequestToPresent(session);
#if 0
    FXL_LOG(INFO) << DumpScenes();  // Handy debugging.
#endif
      });

  // Scene is now set up, send in the input.
  presenter.RunNow([this, compositor_id](scenic::Session* session,
                                         scenic::EntityNode* root_node) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (4,4) location of the 7x7 display.
    // The sequence ends 2x2 diagonally away (north-east) from the touch down.
    session->Enqueue(pointer.Add(4, 4));
    session->Enqueue(pointer.Down(4, 4));
    session->Enqueue(pointer.Move(5, 3));
    session->Enqueue(pointer.Up(6, 2));
    session->Enqueue(pointer.Remove(6, 2));
    RunLoopUntilIdle();
  });

  // Verify client's intake of input events.
  client.ExamineEvents([this](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 6u) << "Should receive exactly 6 input events.";

    // ADD
    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 2, 2));

    // FOCUS
    EXPECT_TRUE(events[1].is_focus());
    EXPECT_TRUE(events[1].focus().focused);

    // DOWN
    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[2].pointer(), 1u, PointerEventPhase::DOWN, 2, 2));

    // MOVE
    EXPECT_TRUE(events[3].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[3].pointer(), 1u, PointerEventPhase::MOVE, 3, 1));

    // UP
    EXPECT_TRUE(events[4].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[4].pointer(), 1u, PointerEventPhase::UP, 4, 0));

    // REMOVE
    EXPECT_TRUE(events[5].is_pointer());
    EXPECT_TRUE(PointerMatches(events[5].pointer(), 1u,
                               PointerEventPhase::REMOVE, 4, 0));
  });
}

}  // namespace lib_ui_input_tests
