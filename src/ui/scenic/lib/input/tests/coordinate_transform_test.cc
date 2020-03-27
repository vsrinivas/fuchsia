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

// This test verifies that rotation is handled correctly when events are delivered to clients.
//
// Below are ASCII diagrams showing the scene setup.
// Note that the notated X,Y coordinate system is the screen coordinate system. The view's
// coordinate system has its origin at corner '1'.
//
// View pre-transformation (1,2,3,4 denote corners of view):
// Note that the view's coordinate system is the same as the screen coordinate system.
//   X ->
// Y 1 O O O 2 - - - -
// | O O O O O - - - -
// v O O O O O - - - -
//   O O O O O - - - -
//   4 O O O 3 - - - -
//   - - - - - - - - -
//   - - - - - - - - -
//   - - - - - - - - -
//   - - - - - - - - -
//
// View post-transformation:
//   X ->
// Y 4A O O O 1D- - - -
// | O  O O O O - - - -
// V O  O O O O - - - -
//   O  O O O O - - - -
//   3U O O O 2M- - - -
//   -  - - - - - - - -
//   -  - - - - - - - -
//   -  - - - - - - - -
//   -  - - - - - - - -
//
// A - Add event
// D - Down event
// M - Move event
// U - Up event

TEST_F(CoordinateTransformTest, Rotated) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  auto [root_session, root_resources] = CreateScene();
  {
    scenic::Session* const session = root_session.session();
    scenic::Scene* const scene = &root_resources.scene;

    scenic::ViewHolder view_holder(session, std::move(view_holder_token), "view_holder");

    view_holder.SetViewProperties(k5x5x1);
    scene->AddChild(view_holder);

    // Rotate the view holder 90 degrees counter-clockwise around the z-axis (which points into
    // screen, so the rotation appears clockwise).
    view_holder.SetAnchor(2.5, 2.5, 0);
    const auto rotation_quaternion = glm::angleAxis(glm::pi<float>() / 2, glm::vec3(0, 0, 1));
    view_holder.SetRotation(rotation_quaternion.x, rotation_quaternion.y, rotation_quaternion.z,
                            rotation_quaternion.w);

    RequestToPresent(session);
  }

  // Client vends a View to the global scene.
  SessionWrapper client = CreateClient("view_1", std::move(view_token));

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation. Injected events are in the global coordinate space.
  {
    scenic::Session* session = root_session.session();

    PointerCommandGenerator pointer(root_resources.compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Add(0.5, 0.5));
    session->Enqueue(pointer.Down(4.5, 0.5));
    session->Enqueue(pointer.Move(4.5, 4.5));
    session->Enqueue(pointer.Up(0.5, 4.5));
  }
  RunLoopUntilIdle();

  {  // Received events should be in the coordinate space of the view.
    const std::vector<InputEvent>& events = client.events();

    EXPECT_EQ(events.size(), 5u);

    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 0.5, 4.5));

    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(PointerMatches(events[2].pointer(), 1u, PointerEventPhase::DOWN, 0.5, 0.5));

    EXPECT_TRUE(events[3].is_pointer());
    EXPECT_TRUE(PointerMatches(events[3].pointer(), 1u, PointerEventPhase::MOVE, 4.5, 0.5));

    EXPECT_TRUE(events[4].is_pointer());
    EXPECT_TRUE(PointerMatches(events[4].pointer(), 1u, PointerEventPhase::UP, 4.5, 4.5));
  }
}

// In this test we set up a view, apply a ClipSpaceTransform to it, and then send pointer events to
// confirm that the coordinates received by the session are correctly transformed.
//
// Below are ASCII diagrams showing the scene setup.
// Note that the notated X,Y coordinate system is the screen coordinate system. The view's
// coordinate system has its origin at corner '1'.
//
// Scene pre-transformation (1,2,3,4 denote the corners of the view):
// Note that the view's coordinate system is the same as the screen coordinate system.
//   X ->
// Y 1 O O O 2 - - - -
// | O O O O O - - - -
// v O O O O O - - - -
//   O O O O O - - - -
//   4 O O O 3 - - - -
//   - - - - - - - - -
//   - - - - - - - - -
//   - - - - - - - - -
//   - - - - - - - - -
//
// Scene after scale, before offset:
// 1   O   O   O   2
//
// O   O   O   O   O
//
// O   O   O - O - O - - - -
//         - - - - - - - - -
// O   O   O - O - O - - - -
//         - - - - - - - - -
// 4   O   O - O - 3 - - - -
//         - - - - - - - - -
//         - - - - - - - - -
//         - - - - - - - - -
//         - - - - - - - - -
//
// Scene post-scale, post-offset:
// The X and Y dimensions of the view are now effectively scaled up to 10x10
// (compared to the 9x9 of the screen), with origin at screen space origin.
//   X ->
// Y 1A- O - D - O - 2
// | - - - - - - - - -
// V O - O - O - O - O
//   - - - - - - - - -
//   U - O - M - O - O
//   - - - - - - - - -
//   O - O - O - O - O
//   - - - - - - - - -
//   4 - O - O - O - 3
//
// A - Add event
// D - Down event
// M - Move event
// U - Up event
TEST_F(CoordinateTransformTest, ClipSpaceTransformed) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  auto [root_session, root_resources] = CreateScene();
  {
    scenic::Session* const session = root_session.session();
    scenic::Scene* const scene = &root_resources.scene;

    scenic::ViewHolder view_holder(session, std::move(view_holder_token), "view_holder");

    view_holder.SetViewProperties(k5x5x1);
    scene->AddChild(view_holder);

    // Set the clip space transform on the camera.
    // The transform scales everything by 2 around the center of the screen (4.5, 4.5) and then
    // applies offsets in Vulkan normalized device coordinates to bring the origin back
    // to where it was originally. (Parameters are in Vulkan Normalized Device Coordinates.)
    root_resources.camera.SetClipSpaceTransform(/*x offset*/ 1, /*y offset*/ 1, /*scale*/ 2);

    RequestToPresent(session);
  }

  // Client vends a View to the global scene.
  SessionWrapper client = CreateClient("view", std::move(view_token));

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation. Injected events are in the screen coordinate space.
  {
    scenic::Session* session = root_session.session();

    PointerCommandGenerator pointer(root_resources.compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Add(0.5, 0.5));
    session->Enqueue(pointer.Down(4.5, 0.5));
    session->Enqueue(pointer.Move(4.5, 4.5));
    session->Enqueue(pointer.Up(0.5, 4.5));
  }
  RunLoopUntilIdle();

  {  // Received events should be in the coordinate space of the view.
    // Expect received coordinates to be half of the injected coordinates, since the view is now
    // effectively twice as big on screen.
    const std::vector<InputEvent>& events = client.events();

    EXPECT_EQ(events.size(), 5u);

    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 0.25, 0.25));

    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(PointerMatches(events[2].pointer(), 1u, PointerEventPhase::DOWN, 2.25, 0.25));

    EXPECT_TRUE(events[3].is_pointer());
    EXPECT_TRUE(PointerMatches(events[3].pointer(), 1u, PointerEventPhase::MOVE, 2.25, 2.25));

    EXPECT_TRUE(events[4].is_pointer());
    EXPECT_TRUE(PointerMatches(events[4].pointer(), 1u, PointerEventPhase::UP, 0.25, 2.25));
  }
}

// In this test we set up a view, apply a ClipSpaceTransform scale to the camera as well as a
// translation on the view holder, and confirm that the delivered coordinates are correctly
// transformed.
//
// Below are ASCII diagrams showing the scene setup.
// Note that the notated X,Y coordinate system is the screen coordinate system. The view's
// coordinate system has its origin at corner '1'.
//
// Scene pre-transformation (1,2,3,4 denote the corners of the view):
// Note that the view's coordinate system is the same as the screen coordinate system.
//   X ->
// Y 1 O O O 2 - - - -
// | O O O O O - - - -
// v O O O O O - - - -
//   O O O O O - - - -
//   4 O O O 3 - - - -
//   - - - - - - - - -
//   - - - - - - - - -
//   - - - - - - - - -
//   - - - - - - - - -
//
// Scene after post-clip space transform, pre-translation:
// 1   O   O   O   2
//
// O   O   O   O   O
//
// O   O   O - O - O - - - -
//         - - - - - - - - -
// O   O   O - O - O - - - -
//         - - - - - - - - -
// 4   O   O - O - 3 - - - -
//         - - - - - - - - -
//         - - - - - - - - -
//         - - - - - - - - -
//         - - - - - - - - -
//
// Scene after post-clip space transform, post-translation:
// Size of view is effectively 10x10, translated by (1,1).
//   X ->
// Y 1   O   O   O   2
// |
// V O   A - O - D - O - -
//       - - - - - - - - -
//   O   O - O - O - O - -
//       - - - - - - - - -
//   O   U - O - M - O - -
//       - - - - - - - - -
//   4   O - O - O - 3 - -
//       - - - - - - - - -
//       - - - - - - - - -
// A - Add event
// D - Down event
// M - Move event
// U - Up event
TEST_F(CoordinateTransformTest, ClipSpaceAndNodeTransformed) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  auto [root_session, root_resources] = CreateScene();
  {
    scenic::Session* const session = root_session.session();
    scenic::Scene* const scene = &root_resources.scene;

    scenic::ViewHolder view_holder(session, std::move(view_holder_token), "view_holder");

    view_holder.SetViewProperties(k5x5x1);
    scene->AddChild(view_holder);

    // Set the clip space transform to zoom in on the center of the screen.
    root_resources.camera.SetClipSpaceTransform(0, 0, /*scale*/ 2);
    // Translate view holder.
    view_holder.SetTranslation(1, 1, 0);

    RequestToPresent(session);
  }

  // Client vends a View to the global scene.
  SessionWrapper client = CreateClient("view", std::move(view_token));

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation. Injected events are in the screen coordinate space.
  {
    scenic::Session* session = root_session.session();

    PointerCommandGenerator pointer(root_resources.compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Add(0.5, 0.5));
    session->Enqueue(pointer.Down(4.5, 0.5));
    session->Enqueue(pointer.Move(4.5, 4.5));
    session->Enqueue(pointer.Up(0.5, 4.5));
  }
  RunLoopUntilIdle();

  {  // Received events should be in the coordinate space of the view.
    const std::vector<InputEvent>& events = client.events();

    EXPECT_EQ(events.size(), 5u);

    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 2.5 - 1, 2.5 - 1));

    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(PointerMatches(events[2].pointer(), 1u, PointerEventPhase::DOWN, 4.5 - 1, 2.5 - 1));

    EXPECT_TRUE(events[3].is_pointer());
    EXPECT_TRUE(PointerMatches(events[3].pointer(), 1u, PointerEventPhase::MOVE, 4.5 - 1, 4.5 - 1));

    EXPECT_TRUE(events[4].is_pointer());
    EXPECT_TRUE(PointerMatches(events[4].pointer(), 1u, PointerEventPhase::UP, 2.5 - 1, 4.5 - 1));
  }
}

}  // namespace
}  // namespace lib_ui_input_tests
