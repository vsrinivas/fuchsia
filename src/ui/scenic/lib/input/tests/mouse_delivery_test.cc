// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async/cpp/time.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/gfx/engine/view_tree.h"
#include "src/ui/scenic/lib/input/tests/util.h"

// This test exercises the event delivery logic for mouse and touchpad events. The mouse moves from
// the bottom left corner to the upper right corner.  While the "down-move-up" sequence should be
// delivered to the focused client, the prefix and suffix "move" events are delivered to the
// top-level client without triggering a focus change.
//
// The geometry of the display and layer are contrained to a 7x7 square. Two 5x5 views are overlayed
// on top; client 1 is higher than client 2 and receives the three prefix "move" events and
// "down-move-up" sequence. Client 2 receives the single suffix "move" event.
//
// We also have the root session add three ShapeNodes on top to emulate mouse cursor placement. To
// save the hassle of moving the cursor around, we simply make the ShapeNodes cover the entire
// screen. The expected behavior is to ignore these mouse cursors, because they do not have an
// owning View.
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
// Event   Mark  Device  View-1      View-2
// Move-1  M     (0,6)   (0.5, 4.5)  n/a
// Move-2  M     (1,5)   (1.5, 3.5)  n/a
// Move-3  M     (2,4)   (2.5, 2.5)  n/a
// Down    D     (3,3)   (3.5, 1.5)  n/a
// Move-4  M     (4,2)   (4.5, 0.5)  n/a
// Up      U     (5,1)   (5.5,-0.5)  n/a
// Move-5  M     (6,0)   n/a         (4.5,0.5)
//
// NOTE: This test is carefully constructed to avoid Vulkan functionality.

namespace lib_ui_input_tests {
namespace {

using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;
using scenic_impl::gfx::ViewTree;

// Class fixture for TEST_F. Sets up a 7x7 "display" for GfxSystem.
class MouseDeliveryTest : public InputSystemTest {
 protected:
  uint32_t test_display_width_px() const override { return 7; }
  uint32_t test_display_height_px() const override { return 7; }
};

TEST_F(MouseDeliveryTest, StandardTest) {
  auto [v1_token, vh1_token] = scenic::ViewTokenPair::New();
  auto [v2_token, vh2_token] = scenic::ViewTokenPair::New();

  // Set up a scene with two views.
  auto [root_session, root_resources] = CreateScene();
  {
    scenic::Session* const session = root_session.session();
    scenic::Scene* const scene = &root_resources.scene;

    // Attach the translated view holders.
    scenic::ViewHolder holder_1(session, std::move(vh1_token), "holder_1"),
        holder_2(session, std::move(vh2_token), "holder_2");

    holder_1.SetViewProperties(k5x5x1);
    holder_2.SetViewProperties(k5x5x1);

    scene->AddChild(holder_1);
    holder_1.SetTranslation(0, 2, -2);

    scene->AddChild(holder_2);
    holder_2.SetTranslation(2, 0, -1);

    // Add three "mouse cursors" to the scene.
    for (int i = 0; i < 3; ++i) {
      scenic::ShapeNode cursor(session);
      cursor.SetTranslation(3, 3, -100);
      cursor.SetLabel("mouse cursor");
      scene->AddChild(cursor);

      scenic::Rectangle rec(session, 7, 7);
      cursor.SetShape(rec);

      scenic::Material material(session);
      cursor.SetMaterial(material);
    }

    RequestToPresent(session);
  }

  SessionWrapper client_1 = CreateClient("View 1", std::move(v1_token)),
                 client_2 = CreateClient("View 2", std::move(v2_token));

  // Scene is now set up, send in the input.
  {
    scenic::Session* const session = root_session.session();

    PointerCommandGenerator pointer(root_resources.compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::MOUSE);
    // A touch sequence that starts at the (0.5,6.5) location of the 7x7 display and
    // ends in the (6.5,0.5) location. Sent in as device (display) coordinates.
    session->Enqueue(pointer.Move(0.5, 6.5));
    session->Enqueue(pointer.Move(1.5, 5.5));
    session->Enqueue(pointer.Move(2.5, 4.5));
    session->Enqueue(pointer.Down(3.5, 3.5));
    session->Enqueue(pointer.Move(4.5, 2.5));
    session->Enqueue(pointer.Up(5.5, 1.5));
    session->Enqueue(pointer.Move(6.5, 0.5));
  }
  RunLoopUntilIdle();

  // Verify client 1's inputs have mouse events.
  {
    const std::vector<InputEvent>& events = client_1.events();

    EXPECT_EQ(events.size(), 7u);

    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::MOVE, 0.5, 4.5));

    EXPECT_TRUE(events[1].is_pointer());
    EXPECT_TRUE(PointerMatches(events[1].pointer(), 1u, PointerEventPhase::MOVE, 1.5, 3.5));

    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(PointerMatches(events[2].pointer(), 1u, PointerEventPhase::MOVE, 2.5, 2.5));

    EXPECT_TRUE(events[3].is_focus());
    EXPECT_TRUE(events[3].focus().focused);

    EXPECT_TRUE(events[4].is_pointer());
    EXPECT_TRUE(PointerMatches(events[4].pointer(), 1u, PointerEventPhase::DOWN, 3.5, 1.5));

    EXPECT_TRUE(events[5].is_pointer());
    EXPECT_TRUE(PointerMatches(events[5].pointer(), 1u, PointerEventPhase::MOVE, 4.5, 0.5));

    EXPECT_TRUE(events[6].is_pointer());
    EXPECT_TRUE(PointerMatches(events[6].pointer(), 1u, PointerEventPhase::UP, 5.5, -0.5));
  }

  // Verify client 2's input has one mouse event.
  {
    const std::vector<InputEvent>& events = client_2.events();

    EXPECT_EQ(events.size(), 1u);

    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::MOVE, 4.5, 0.5));
  }
}

TEST_F(MouseDeliveryTest, OffViewClickTriggersUnfocusEvent) {
  auto [v1_token, vh1_token] = scenic::ViewTokenPair::New();
  auto [v2_token, vh2_token] = scenic::ViewTokenPair::New();

  // Set up a scene with two views.
  auto [root_session, root_resources] = CreateScene();
  {
    scenic::Session* const session = root_session.session();
    scenic::Scene* const scene = &root_resources.scene;

    // Attach the translated view holders.
    scenic::ViewHolder holder_1(session, std::move(vh1_token), "holder_1"),
        holder_2(session, std::move(vh2_token), "holder_2");

    holder_1.SetViewProperties(k5x5x1);
    holder_2.SetViewProperties(k5x5x1);

    scene->AddChild(holder_1);
    holder_1.SetTranslation(0, 2, -2);

    scene->AddChild(holder_2);
    holder_2.SetTranslation(2, 0, -1);

    // Add three "mouse cursors" to the scene.
    for (int i = 0; i < 3; ++i) {
      scenic::ShapeNode cursor(session);
      cursor.SetTranslation(3, 3, -100);
      cursor.SetLabel("mouse cursor");
      scene->AddChild(cursor);

      scenic::Rectangle rec(session, 7, 7);
      cursor.SetShape(rec);

      scenic::Material material(session);
      cursor.SetMaterial(material);
    }

    RequestToPresent(session);
  }

  SessionWrapper client_1 = CreateClient("View 1", std::move(v1_token)),
                 client_2 = CreateClient("View 2", std::move(v2_token));

  // Transfer focus to view 1.
  zx_koid_t root_koid = engine()->scene_graph()->view_tree().focus_chain()[0];
  auto status = engine()->scene_graph()->RequestFocusChange(root_koid, client_1.ViewKoid());
  ASSERT_EQ(status, ViewTree::FocusChangeStatus::kAccept);

  RunLoopUntilIdle();

  root_session.events().clear();
  client_1.events().clear();
  client_2.events().clear();

  // Send in input to display corner: clients receive no mouse events, and root session (the
  // presenter) receives the focus event.
  {
    scenic::Session* const session = root_session.session();

    PointerCommandGenerator pointer(root_resources.compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::MOUSE);
    // A touch sequence that starts at the (0,0) location of the 7x7 display; sent in as device
    // (display) coordinates.
    session->Enqueue(pointer.Down(0, 0));
  }
  RunLoopUntilIdle();

  // Verify client 1 sees just the unfocus event.
  {
    const std::vector<InputEvent>& events = client_1.events();

    ASSERT_EQ(events.size(), 1u);

    EXPECT_TRUE(events[0].is_focus());
    EXPECT_FALSE(events[0].focus().focused);
  }

  // Verify client 2 sees nothing.
  {
    const std::vector<InputEvent>& events = client_2.events();
    EXPECT_EQ(events.size(), 0u);
  }

  // Verify root session sees just the focus event.
  {
    const std::vector<InputEvent>& events = root_session.events();

    ASSERT_EQ(events.size(), 1u);

    EXPECT_TRUE(events[0].is_focus());
    EXPECT_TRUE(events[0].focus().focused);

  }
}

TEST_F(MouseDeliveryTest, NoFocusTest) {
  auto [v1_token, vh1_token] = scenic::ViewTokenPair::New();
  auto [v2_token, vh2_token] = scenic::ViewTokenPair::New();

  // Set up a scene with two views.
  auto [root_session, root_resources] = CreateScene();
  {
    scenic::Session* const session = root_session.session();
    scenic::Scene* const scene = &root_resources.scene;

    // Attach the translated view holders.
    scenic::ViewHolder holder_1(session, std::move(vh1_token), "holder_1"),
        holder_2(session, std::move(vh2_token), "holder_2");

    // View 1 may not receive focus.
    holder_1.SetViewProperties({.bounding_box = {.max = {5, 5, 1}}, .focus_change = false});
    holder_2.SetViewProperties(k5x5x1);

    scene->AddChild(holder_1);
    holder_1.SetTranslation(0, 2, -2);

    scene->AddChild(holder_2);
    holder_2.SetTranslation(2, 0, -1);

    // Add three "mouse cursors" to the scene.
    for (int i = 0; i < 3; ++i) {
      scenic::ShapeNode cursor(session);
      cursor.SetTranslation(3, 3, -100);
      cursor.SetLabel("mouse cursor");
      scene->AddChild(cursor);

      scenic::Rectangle rec(session, 7, 7);
      cursor.SetShape(rec);

      scenic::Material material(session);
      cursor.SetMaterial(material);
    }

    RequestToPresent(session);
  }

  SessionWrapper client_1 = CreateClient("View 1", std::move(v1_token)),
                 client_2 = CreateClient("View 2", std::move(v2_token));

  // Scene is now set up, send in the input.
  {
    scenic::Session* const session = root_session.session();

    PointerCommandGenerator pointer(root_resources.compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::MOUSE);
    // A touch sequence that starts at the (0.5,6.5) location of the 7x7 display and
    // ends in the (6.5,0.5) location. Sent in as device (display) coordinates.
    session->Enqueue(pointer.Move(0.5, 6.5));
    session->Enqueue(pointer.Move(1.5, 5.5));
    session->Enqueue(pointer.Move(2.5, 4.5));
    session->Enqueue(pointer.Down(3.5, 3.5));
    session->Enqueue(pointer.Move(4.5, 2.5));
    session->Enqueue(pointer.Up(5.5, 1.5));
    session->Enqueue(pointer.Move(6.5, 0.5));
  }
  RunLoopUntilIdle();

  // Verify client 1's inputs have mouse events.
  {
    const std::vector<InputEvent>& events = client_1.events();

    EXPECT_EQ(events.size(), 6u);

    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::MOVE, 0.5, 4.5));

    EXPECT_TRUE(events[1].is_pointer());
    EXPECT_TRUE(PointerMatches(events[1].pointer(), 1u, PointerEventPhase::MOVE, 1.5, 3.5));

    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(PointerMatches(events[2].pointer(), 1u, PointerEventPhase::MOVE, 2.5, 2.5));

    EXPECT_TRUE(events[3].is_pointer());
    EXPECT_TRUE(PointerMatches(events[3].pointer(), 1u, PointerEventPhase::DOWN, 3.5, 1.5));

    EXPECT_TRUE(events[4].is_pointer());
    EXPECT_TRUE(PointerMatches(events[4].pointer(), 1u, PointerEventPhase::MOVE, 4.5, 0.5));

    EXPECT_TRUE(events[5].is_pointer());
    EXPECT_TRUE(PointerMatches(events[5].pointer(), 1u, PointerEventPhase::UP, 5.5, -0.5));
  }

  // Verify client 2's input has one mouse event.
  {
    const std::vector<InputEvent>& events = client_2.events();

    EXPECT_EQ(events.size(), 1u);

    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::MOVE, 4.5, 0.5));
  }
}

}  // namespace
}  // namespace lib_ui_input_tests
