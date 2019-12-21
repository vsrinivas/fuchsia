// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/input/tests/util.h"

// These tests exercise the coordinate transform logic applied to pointer events sent to sessions.

namespace lib_ui_input_tests {
namespace {

using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;

// Sets up a 9x9 "display".
class CoordinateTransformTest : public InputSystemTest {
 protected:
  uint32_t test_display_width_px() const override { return 9; }
  uint32_t test_display_height_px() const override { return 9; }
};

// In this test, we set up a scene with two translated but overlapping Views, and see if events are
// conveyed to the client in an appropriate way.
//
// The geometry is constrained to a 9x9 display and layer, with two 5x5 rectangles that intersect in
// one pixel, like so:
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
// To create this test setup, we perform translation of each View itself (i.e., (0,0) and (4,4)), in
// addition to aligning (translating) each View's Shape to its owning View.
//
// View 1 creates its rectangle in the upper left quadrant; the View's origin is marked 'x'.
// Similarly, View 2 creates its rectangle in the bottom right quadrant; the View's origin marked
// 'y'.
//
// The hit test occurs at the center of the screen (colocated with View 2's origin at 'y'), at (4,4)
// in device space. The touch events move diagonally up and to the right, and we have the following
// correspondence of coordinates:
//
// Event  Mark  Device      View-1      View-2
// ADD    y     (4.5,4.5)   (4.5,4.5)   (0.5, 0.5)
// DOWN   y     (4.5,4.5)   (4.5,4.5)   (0.5, 0.5)
// MOVE   M     (5.5,3.5)   (5.5,3.5)   (1.5,-0.5)
// UP     U     (6.5,2.5)   (6.5,2.5)   (2.5,-1.5)
// REMOVE U     (6.5,2.5)   (6.5,2.5)   (2.5,-1.5)
//
// N.B. View 1 sits *above* View 2 in elevation; hence, View 1 should receive the focus event.
//
// N.B. This test is carefully constructed to avoid Vulkan functionality.
TEST_F(CoordinateTransformTest, Translated) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up a scene with two ViewHolders.
  auto [root_session, root_resources] = CreateScene();
  {
    scenic::Session* const session = root_session.session();
    scenic::Scene* const scene = &root_resources.scene;

    // Attach two translated ViewHolders.
    scenic::ViewHolder holder_1(session, std::move(vh1), "holder_1"),
        holder_2(session, std::move(vh2), "holder_2");

    holder_1.SetViewProperties(k5x5x1);
    holder_2.SetViewProperties(k5x5x1);

    scene->AddChild(holder_1);
    holder_1.SetTranslation(0, 0, -2);

    scene->AddChild(holder_2);
    holder_2.SetTranslation(4, 4, -1);

    RequestToPresent(session);
  }

  // Clients each vend a View to the global scene.
  SessionWrapper client_1 = CreateClient("view_1", std::move(v1)),
                 client_2 = CreateClient("view_2", std::move(v2));

  // Multi-agent scene is now set up, send in the input.
  {
    scenic::Session* session = root_session.session();

    PointerCommandGenerator pointer(root_resources.compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts in the direct center of the 9x9 display.
    // The sequence ends 2x2 diagonally away (north-east) from the touch down.
    // Note that although this gesture escapes the bounds of view 1, we expect delivery to be
    // latched to it due to it being under DOWN.
    session->Enqueue(pointer.Add(4.5, 4.5));
    session->Enqueue(pointer.Down(4.5, 4.5));
    session->Enqueue(pointer.Move(5.5, 3.5));
    session->Enqueue(pointer.Up(6.5, 2.5));
    session->Enqueue(pointer.Remove(6.5, 2.5));
  }
  RunLoopUntilIdle();

  {
    const std::vector<InputEvent>& events = client_1.events();

    EXPECT_EQ(events.size(), 6u);

    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 4.5, 4.5));

    EXPECT_TRUE(events[1].is_focus());
    EXPECT_TRUE(events[1].focus().focused);

    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(PointerMatches(events[2].pointer(), 1u, PointerEventPhase::DOWN, 4.5, 4.5));

    EXPECT_TRUE(events[3].is_pointer());
    EXPECT_TRUE(PointerMatches(events[3].pointer(), 1u, PointerEventPhase::MOVE, 5.5, 3.5));

    EXPECT_TRUE(events[4].is_pointer());
    EXPECT_TRUE(PointerMatches(events[4].pointer(), 1u, PointerEventPhase::UP, 6.5, 2.5));

    EXPECT_TRUE(events[5].is_pointer());
    EXPECT_TRUE(PointerMatches(events[5].pointer(), 1u, PointerEventPhase::REMOVE, 6.5, 2.5));
  }

  {
    const std::vector<InputEvent>& events = client_2.events();

    EXPECT_EQ(events.size(), 5u);

    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 0.5, 0.5));

    EXPECT_TRUE(events[1].is_pointer());
    EXPECT_TRUE(PointerMatches(events[1].pointer(), 1u, PointerEventPhase::DOWN, 0.5, 0.5));

    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(PointerMatches(events[2].pointer(), 1u, PointerEventPhase::MOVE, 1.5, -0.5));

    EXPECT_TRUE(events[3].is_pointer());
    EXPECT_TRUE(PointerMatches(events[3].pointer(), 1u, PointerEventPhase::UP, 2.5, -1.5));

    EXPECT_TRUE(events[4].is_pointer());
    EXPECT_TRUE(PointerMatches(events[4].pointer(), 1u, PointerEventPhase::REMOVE, 2.5, -1.5));

#if 0
    for (const auto& event : events)
      FXL_LOG(INFO) << "Client got: " << event;  // Handy debugging.
#endif
  }
}

// This test verifies scaling applied to a view subgraph behind another.
TEST_F(CoordinateTransformTest, ScaledBehind) {
  // v1 is in front, not scaled
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  // v2 is in back but scaled 4x
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up a scene with two ViewHolders.
  auto [root_session, root_resources] = CreateScene();
  {
    scenic::Session* const session = root_session.session();
    scenic::Scene* const scene = &root_resources.scene;

    scenic::ViewHolder holder_1(session, std::move(vh1), "holder_1"),
        holder_2(session, std::move(vh2), "holder_2");

    holder_1.SetViewProperties(k5x5x1);
    holder_1.SetTranslation(1, 1, -5);
    holder_2.SetViewProperties(k5x5x1);
    holder_2.SetTranslation(1, 1, -4);
    holder_2.SetScale(4, 4, 4);

    scene->AddChild(holder_1);
    scene->AddChild(holder_2);

    RequestToPresent(session);
  }

  // Clients each vend a View to the global scene.
  SessionWrapper client_1 = CreateClient("view_1", std::move(v1)),
                 client_2 = CreateClient("view_2", std::move(v2));

  // Multi-agent scene is now set up, send in the input.
  {
    scenic::Session* session = root_session.session();

    PointerCommandGenerator pointer(root_resources.compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // Touch once at (2.5, 2.5)
    session->Enqueue(pointer.Add(2.5, 2.5));
    session->Enqueue(pointer.Down(2.5, 2.5));
  }
  RunLoopUntilIdle();

  {
    const std::vector<InputEvent>& events = client_1.events();

    EXPECT_EQ(events.size(), 3u);

    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 1.5, 1.5));

    EXPECT_TRUE(events[1].is_focus());
    EXPECT_TRUE(events[1].focus().focused);

    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(PointerMatches(events[2].pointer(), 1u, PointerEventPhase::DOWN, 1.5, 1.5));
  }

  {
    const std::vector<InputEvent>& events = client_2.events();

    EXPECT_EQ(events.size(), 2u);

    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 1.5 / 4, 1.5 / 4));

    EXPECT_TRUE(events[1].is_pointer());
    EXPECT_TRUE(PointerMatches(events[1].pointer(), 1u, PointerEventPhase::DOWN, 1.5 / 4, 1.5 / 4));
  }
}

// This test verifies scaling applied to a view subgraph in front of another.
TEST_F(CoordinateTransformTest, ScaledInFront) {
  // v1 is in front and scaled 4x
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  // v2 is in back but not scaled
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up a scene with two ViewHolders.
  auto [root_session, root_resources] = CreateScene();
  {
    scenic::Session* const session = root_session.session();
    scenic::Scene* const scene = &root_resources.scene;

    scenic::ViewHolder holder_1(session, std::move(vh1), "holder_1"),
        holder_2(session, std::move(vh2), "holder_2");

    holder_1.SetViewProperties(k5x5x1);
    holder_1.SetTranslation(1, 1, -5);
    holder_1.SetScale(4, 4, 4);
    holder_2.SetViewProperties(k5x5x1);
    holder_2.SetTranslation(1, 1, -1);

    scene->AddChild(holder_1);
    scene->AddChild(holder_2);

    RequestToPresent(session);
  }

  // Clients each vend a View to the global scene.
  SessionWrapper client_1 = CreateClient("view_1", std::move(v1)),
                 client_2 = CreateClient("view_2", std::move(v2));

  // Multi-agent scene is now set up, send in the input.
  {
    scenic::Session* session = root_session.session();

    PointerCommandGenerator pointer(root_resources.compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // Touch once at (2.5, 2.5)
    session->Enqueue(pointer.Add(2.5, 2.5));
    session->Enqueue(pointer.Down(2.5, 2.5));
  }
  RunLoopUntilIdle();

  {
    const std::vector<InputEvent>& events = client_1.events();

    EXPECT_EQ(events.size(), 3u);

    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 1.5 / 4, 1.5 / 4));

    EXPECT_TRUE(events[1].is_focus());
    EXPECT_TRUE(events[1].focus().focused);

    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(PointerMatches(events[2].pointer(), 1u, PointerEventPhase::DOWN, 1.5 / 4, 1.5 / 4));
  }

  {
    const std::vector<InputEvent>& events = client_2.events();

    EXPECT_EQ(events.size(), 2u);

    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 1.5, 1.5));

    EXPECT_TRUE(events[1].is_pointer());
    EXPECT_TRUE(PointerMatches(events[1].pointer(), 1u, PointerEventPhase::DOWN, 1.5, 1.5));
  }
}

}  // namespace
}  // namespace lib_ui_input_tests
