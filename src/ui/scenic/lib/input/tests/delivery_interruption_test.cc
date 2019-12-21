// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <memory>

#include <gtest/gtest.h>

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

namespace lib_ui_input_tests {
namespace {

using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;

// Class fixture for TEST_F. Sets up a 5x5 "display" for GfxSystem.
class DeliveryInterruptionTest : public InputSystemTest {
 protected:
  uint32_t test_display_width_px() const override { return 5; }
  uint32_t test_display_height_px() const override { return 5; }
};

TEST_F(DeliveryInterruptionTest, SessionDied) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [root_session, root_resources] = CreateScene();
  uint32_t compositor_id = root_resources.compositor.id();

  {
    scenic::Session* const session = root_session.session();
    scenic::ViewHolder holder(session, std::move(view_holder_token), "view holder");

    // NOTE: The view holder itself does not require translation to be aligned with the layer.
    holder.SetViewProperties(k5x5x1);

    root_resources.scene.AddChild(holder);
    RequestToPresent(session);
  }

  {
    SessionWrapper client = CreateClient("view", std::move(view_token));

    // Scene is now set up, send in the input.
    {
      scenic::Session* const session = root_session.session();

      PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                      /*pointer id*/ 1, PointerEventType::TOUCH);
      // Sent in as device (display) coordinates.
      session->Enqueue(pointer.Add(2.5, 2.5));
      session->Enqueue(pointer.Down(2.5, 2.5));

      RequestToPresent(session);
    }

    // Verify client's inputs have expected touch events.
    {
      const std::vector<InputEvent>& events = client.events();

      EXPECT_EQ(events.size(), 3u);

      EXPECT_TRUE(events[0].is_pointer());
      EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 2.5, 2.5));

      EXPECT_TRUE(events[1].is_focus());
      EXPECT_TRUE(events[1].focus().focused);

      EXPECT_TRUE(events[2].is_pointer());
      EXPECT_TRUE(PointerMatches(events[2].pointer(), 1u, PointerEventPhase::DOWN, 2.5, 2.5));
    }
  }

  // The client's session has now gone out of scope. Scenic's input system still has a latch onto
  // this session.
  RunLoopUntilIdle();

  {
    scenic::Session* const session = root_session.session();

    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1, /*pointer id*/ 1,
                                    PointerEventType::TOUCH);
    // Sent in as device (display) coordinates.
    session->Enqueue(pointer.Move(2, 2));

    RequestToPresent(session);
  }

  // No crash.
}

}  // namespace
}  // namespace lib_ui_input_tests
