// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async/cpp/time.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/ui/input/cpp/formatting.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/clock.h>
#include <lib/zx/eventpair.h>

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/input/tests/util.h"

// This test exercises dispatch logic when a session goes out of scope.  A dead session can
// manifest, for example, as a null EventReporter*.
//
// The geometry of the display and layer are constrained to a 5x5 square. Just one 5x5 view is
// overlaid on top, and one rect shape placed in the center to be visible to the hit tester.
//
// Touch events are sent to the center of the display. When the session goes out of scope,
// subsequent touch events should *not* induce a crash.
//
// We have the following correspondence of coordinates:
//
// Event   Device     View     Notes
// Add     (2,2)   (2.5, 2.5)  Initial hit test
// Down    (2,2)   (2.5, 2.5)  Latch for future MOVE events
// ---- (session death) ----
// Move    (2,2)      N/A
//
// NOTE: This test is carefully constructed to avoid Vulkan functionality.

namespace lib_input_tests {

using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;
using lib_ui_input_tests::PointerCommandGenerator;
using lib_ui_input_tests::PointerMatches;
using lib_ui_input_tests::SessionWrapper;

// Class fixture for TEST_F. Sets up a 5x5 "display" for GfxSystem.
class DeliveryInterruptionTest : public lib_ui_input_tests::InputSystemTest {
 protected:
  uint32_t test_display_width_px() const override { return 5; }
  uint32_t test_display_height_px() const override { return 5; }
};

TEST_F(DeliveryInterruptionTest, SessionDied) {
  auto pair = scenic::ViewTokenPair::New();

  // "Presenter" sets up a scene with one view.
  // We require a separate presenter session, specifically to perform input injection.
  struct Presenter : public SessionWrapper {
    Presenter(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}
    std::unique_ptr<scenic::Compositor> compositor;
  } presenter(scenic());  // Persistent; stack storage.

  presenter.RunNow(
      [test = this, state = &presenter, holder_token = std::move(pair.view_holder_token)](
          scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
        // Minimal scene.
        state->compositor = std::make_unique<scenic::Compositor>(session);

        scenic::Scene scene(session);
        scenic::Camera camera(scene);
        scenic::Renderer renderer(session);
        renderer.SetCamera(camera);

        scenic::Layer layer(session);
        layer.SetSize(test->test_display_width_px(), test->test_display_height_px());
        layer.SetRenderer(renderer);

        scenic::LayerStack layer_stack(session);
        layer_stack.AddLayer(layer);
        state->compositor->SetLayerStack(layer_stack);

        // Add local root node to the scene.
        scene.AddChild(*session_anchor);
        scenic::ViewHolder holder(session, std::move(holder_token), "view holder");

        // Create the view bounds.
        // NOTE: The view holder itself does not require translation to be aligned with the layer.
        const float kZero[3] = {0, 0, 0};
        holder.SetViewProperties(kZero, (float[3]){5, 5, 1}, kZero, kZero);

        // Attach to the session's entity node.
        session_anchor->Attach(holder);

        test->RequestToPresent(session);
      });

  struct Client : public SessionWrapper {
    Client(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}
    std::unique_ptr<scenic::View> view;
  };

  std::unique_ptr<Client> client = std::make_unique<Client>(scenic());  // Limited lifetime; heap.

  // Client sets up its content.
  client->RunNow([test = this, state = client.get(), view_token = std::move(pair.view_token)](
                     scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
    auto pair = scenic::ViewRefPair::New();
    state->view =
        std::make_unique<scenic::View>(session, std::move(view_token), std::move(pair.control_ref),
                                       std::move(pair.view_ref), "view");
    state->view->AddChild(*session_anchor);

    scenic::ShapeNode shape(session);
    shape.SetTranslation(2, 2, 0);  // Center the shape within the view.
    session_anchor->AddChild(shape);

    scenic::Rectangle rec(session, 5, 5);
    shape.SetShape(rec);

    scenic::Material material(session);
    shape.SetMaterial(material);

    test->RequestToPresent(session);
  });

  // Scene is now set up, send in the input.
  presenter.RunNow([test = this, compositor_id = presenter.compositor->id()](
                       scenic::Session* session, scenic::EntityNode* session_anchor) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1, /*pointer id*/ 1,
                                    PointerEventType::TOUCH);
    // Sent in as device (display) coordinates.
    session->Enqueue(pointer.Add(2, 2));
    session->Enqueue(pointer.Down(2, 2));

    test->RequestToPresent(session);
  });

  // Verify client's inputs have expected touch events.
  client->ExamineEvents([](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 3u) << "Should receive exactly 3 input events.";

    // ADD
    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 2.5, 2.5));

    // FOCUS
    EXPECT_TRUE(events[1].is_focus());
    EXPECT_TRUE(events[1].focus().focused);

    // DOWN
    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(PointerMatches(events[2].pointer(), 1u, PointerEventPhase::DOWN, 2.5, 2.5));
  });

  // Destroy the client's session. Scenic's input system still has a latch onto this session.
  client = nullptr;
  RunLoopUntilIdle();

  presenter.RunNow([test = this, compositor_id = presenter.compositor->id()](
                       scenic::Session* session, scenic::EntityNode* session_anchor) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1, /*pointer id*/ 1,
                                    PointerEventType::TOUCH);
    // Sent in as device (display) coordinates.
    session->Enqueue(pointer.Move(2, 2));

    test->RequestToPresent(session);
  });

  // No crash.
}

}  // namespace lib_input_tests
