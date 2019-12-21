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
// Event  Finger  Mark  Device  View-1     View-2
// ADD    1       y     (0,0)   (0.5,0.5)  n/a
// DOWN   1       y     (0,0)   (0.5,0.5)  n/a
//
// The second hit test occurs at the overlap, at 'y'.  Typically, View 2 would
// receive a focus event, and View 1 would receive an unfocus event.  Since View
// 2 has the focus avoidance property, each View should receive the pointer
// events, but each View should *not* receive a focus or unfocus event.  The
// coordinates are:
//
// Event  Finger  Mark  Device  View-1     View-2
// ADD    2       y     (4,4)   (4.5,4.5)  (0.5, 0.5)
// DOWN   2       y     (4,4)   (4.5,4.5)  (0.5, 0.5)
//
// We use a different finger ID to trigger the second hit test. Each finger's
// state sequence is thus consistent, albeit incomplete for test brevity.
//
// N.B. This test is carefully constructed to avoid Vulkan functionality.

namespace lib_ui_input_tests {
namespace {

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
  auto [v_1, vh_1] = scenic::ViewTokenPair::New();
  auto [v_2, vh_2] = scenic::ViewTokenPair::New();

  // Set up a scene with room for two Views.
  auto [root_session, root_resources] = CreateScene();
  {
    scenic::Session* const session = root_session.session();
    scenic::Scene* const scene = &root_resources.scene;

    // Add per-view translation for each View, hang the ViewHolders.
    scenic::ViewHolder holder_1(session, std::move(vh_1), "view holder 1"),
        holder_2(session, std::move(vh_2), "view holder 2");

    holder_1.SetViewProperties(k5x5x1);
    // Set "no-focus" property for view 2.
    holder_2.SetViewProperties({.bounding_box = {.max = {5, 5, 1}}, .focus_change = false});

    scene->AddChild(holder_1);
    holder_1.SetTranslation(0, 0, -1);

    scene->AddChild(holder_2);
    holder_2.SetTranslation(4, 4, -2);

    RequestToPresent(session);
  }

  SessionWrapper client_1 = CreateClient("view 1", std::move(v_1)),
                 client_2 = CreateClient("view 2", std::move(v_2));

  // Multi-agent scene is now set up, send in the input.
  {
    scenic::Session* const session = root_session.session();
    const uint32_t compositor_id = root_resources.compositor.id();

    PointerCommandGenerator pointer_1(compositor_id, /*device id*/ 1,
                                      /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts in the upper left corner of the display.
    session->Enqueue(pointer_1.Add(0.5, 0.5));
    session->Enqueue(pointer_1.Down(0.5, 0.5));

    PointerCommandGenerator pointer_2(compositor_id, /*device id*/ 1,
                                      /*pointer id*/ 2, PointerEventType::TOUCH);
    // A touch sequence that starts in the middle of the display.
    session->Enqueue(pointer_2.Add(4.5, 4.5));
    session->Enqueue(pointer_2.Down(4.5, 4.5));
  }
  RunLoopUntilIdle();

  {
    const std::vector<InputEvent>& events = client_1.events();

    EXPECT_EQ(events.size(), 5u);

    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 0.5, 0.5));

    EXPECT_TRUE(events[1].is_focus());
    EXPECT_TRUE(events[1].focus().focused);

    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(PointerMatches(events[2].pointer(), 1u, PointerEventPhase::DOWN, 0.5, 0.5));

    EXPECT_TRUE(events[3].is_pointer());
    EXPECT_TRUE(PointerMatches(events[3].pointer(), 2u, PointerEventPhase::ADD, 4.5, 4.5));

    // No unfocus event here!

    EXPECT_TRUE(events[4].is_pointer());
    EXPECT_TRUE(PointerMatches(events[4].pointer(), 2u, PointerEventPhase::DOWN, 4.5, 4.5));
  }

  {
    const std::vector<InputEvent>& events = client_2.events();

    EXPECT_EQ(events.size(), 2u);

    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 2u, PointerEventPhase::ADD, 0.5, 0.5));

    // No focus event here!

    EXPECT_TRUE(events[1].is_pointer());
    EXPECT_TRUE(PointerMatches(events[1].pointer(), 2u, PointerEventPhase::DOWN, 0.5, 0.5));

#if 0
    for (const auto& event : events)
      FXL_LOG(INFO) << "Client got: " << event;  // Handy debugging.
#endif
  }
}

}  // namespace
}  // namespace lib_ui_input_tests
