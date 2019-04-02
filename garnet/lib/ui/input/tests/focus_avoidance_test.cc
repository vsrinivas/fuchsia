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

// This test exercises the focus avoidance property of a View.  A pointer DOWN
// event typically triggers a pair of focus/unfocus events (each sent to a
// client).  A View that has the focus avoidance property, and that would
// otherwise trigger focus/unfocus events, should not trigger these events.  We
// set up a scene with two translated but overlapping Views, and see if
// focus/unfocus events are not conveyed to each client.
//
// The geometry is constrained to a 9x9 display and layer, with two 5x5
// rectangles that intersect in one pixel, like so:
//
//     x 1 1 1 1 - - - -
//     1 1 1 1 1 - - - -
//     1 1 1 1 1 - - - -
//     1 1 1 1 1 - - - -
//     1 1 1 1 y 2 2 2 2
//     - - - - 2 2 2 2 2
//     - - - - 2 2 2 2 2
//     - - - - 2 2 2 2 2      x - View 1 origin
//     - - - - 2 2 2 2 2      y - View 2 origin
//
// To create this test setup, we perform translation of each View itself (i.e.,
// (0,0) and (4,4)), in addition to aligning (translating) each View's Shape to
// its owning View. The setup also sets the focus avoidance property for View 2.
//
// View 1 creates its rectangle in the upper left quadrant; its origin is marked
// 'x'. Similarly, View 2 creates its rectangle in the bottom right quadrant;
// its origin marked 'y'. Here, View 1 is *underneath* View 2; the top-most
// pixel at 'y' belongs to View 2.
//
// The first hit test occurs at 'x' to ensure View 1 gains focus. The
// coordinates are:
//
// Event  Finger  Mark  Device  View-1  View-2
// ADD    1       y     (0,0)   (0,0)   n/a
// DOWN   1       y     (0,0)   (0,0)   n/a
//
// The second hit test occurs at the overlap, at 'y'.  Typically, View 2 would
// receive a focus event, and View 1 would receive an unfocus event.  Since View
// 2 has the focus avoidance property, each View should receive the pointer
// events, but each View should *not* receive a focus or unfocus event.  The
// coordinates are:
//
// Event  Finger  Mark  Device  View-1  View-2
// ADD    2       y     (4,4)   (4,4)   (0, 0)
// DOWN   2       y     (4,4)   (4,4)   (0, 0)
//
// We use a different finger ID to trigger the second hit test. Each finger's
// state sequence is thus consistent, albeit incomplete for test brevity.
//
// N.B. This test is carefully constructed to avoid Vulkan functionality.

namespace lib_ui_input_tests {

using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;

// Class fixture for TEST_F. Sets up a 9x9 "display" for GfxSystem.
class FocusAvoidanceTest : public InputSystemTest {
 protected:
  uint32_t test_display_width_px() const override { return 9; }
  uint32_t test_display_height_px() const override { return 9; }
};

TEST_F(FocusAvoidanceTest, ViewHierarchyByScenic) {
  // Create the tokens for the Presenter to share with each client.
  zx::eventpair vh_1, v_1, vh_2, v_2;
  CreateTokenPair(&vh_1, &v_1);
  CreateTokenPair(&vh_2, &v_2);

  // "Presenter" sets up a scene with room for two Views.
  uint32_t compositor_id = 0;
  SessionWrapper presenter(scenic());
  presenter.RunNow(
      [this, &compositor_id, vh_1 = std::move(vh_1), vh_2 = std::move(vh_2)](
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

        // Add local root node to the scene. Add per-view translation for each
        // View, hang the ViewHolders.
        scene.AddChild(*root_node);
        scenic::EntityNode translate_1(session), translate_2(session);
        scenic::ViewHolder holder_1(session, std::move(vh_1), "view holder 1"),
            holder_2(session, std::move(vh_2), "view holder 2");

        root_node->AddChild(translate_1);
        translate_1.SetTranslation(0, 0, -1);
        translate_1.Attach(holder_1);

        root_node->AddChild(translate_2);
        translate_2.SetTranslation(4, 4, -2);
        translate_2.Attach(holder_2);

        // View 2's parent (Presenter) sets "no-focus" property for view 2.
        {
          fuchsia::ui::gfx::ViewProperties properties;
          properties.focus_change = false;
          holder_2.SetViewProperties(std::move(properties));
        }

        RequestToPresent(session);
      });

  // Client 1 vends a View to the global scene.
  SessionWrapper client_1(scenic());
  client_1.RunNow(
      [this, v_1 = std::move(v_1)](scenic::Session* session,
                                   scenic::EntityNode* root_node) mutable {
        scenic::View view(session, std::move(v_1), "view 1");
        view.AddChild(*root_node);

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
      [this, v_2 = std::move(v_2)](scenic::Session* session,
                                   scenic::EntityNode* root_node) mutable {
        scenic::View view(session, std::move(v_2), "view 2");
        view.AddChild(*root_node);

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
    PointerCommandGenerator pointer_1(compositor_id, /*device id*/ 1,
                                      /*pointer id*/ 1,
                                      PointerEventType::TOUCH);
    // A touch sequence that starts in the upper left corner of the display.
    session->Enqueue(pointer_1.Add(0, 0));
    session->Enqueue(pointer_1.Down(0, 0));

    PointerCommandGenerator pointer_2(compositor_id, /*device id*/ 1,
                                      /*pointer id*/ 2,
                                      PointerEventType::TOUCH);
    // A touch sequence that starts in the middle of the display.
    session->Enqueue(pointer_2.Add(4, 4));
    session->Enqueue(pointer_2.Down(4, 4));

    RunLoopUntilIdle();

#if 0
    FXL_LOG(INFO) << DumpScenes();  // Handy debugging.
#endif
  });

  client_1.ExamineEvents([this](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 5u) << "Should receive exactly 5 input events.";

    // ADD
    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 0, 0));

    // FOCUS
    EXPECT_TRUE(events[1].is_focus());
    EXPECT_TRUE(events[1].focus().focused);

    // DOWN
    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[2].pointer(), 1u, PointerEventPhase::DOWN, 0, 0));

    // ADD
    EXPECT_TRUE(events[3].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[3].pointer(), 2u, PointerEventPhase::ADD, 4, 4));

    // No unfocus event here!

    // DOWN
    EXPECT_TRUE(events[4].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[4].pointer(), 2u, PointerEventPhase::DOWN, 4, 4));
  });

  client_2.ExamineEvents([this](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 2u) << "Should receive exactly 2 input events.";

    // ADD
    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[0].pointer(), 2u, PointerEventPhase::ADD, 0, 0));

    // No focus event here!

    // DOWN
    EXPECT_TRUE(events[1].is_pointer());
    EXPECT_TRUE(
        PointerMatches(events[1].pointer(), 2u, PointerEventPhase::DOWN, 0, 0));

#if 0
    for (const auto& event : events)
      FXL_LOG(INFO) << "Client got: " << event;  // Handy debugging.
#endif
  });
}

}  // namespace lib_ui_input_tests
