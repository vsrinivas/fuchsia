// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/input/tests/util.h"

// This test exercises the event delivery logic for hard keyboard events.
//
// Typically, hard keyboard events are sent to the Text Sync service for further
// dispatch to an IME; in contrast, the hard keyboard events are not sent
// directly to a View. This is the default behavior.
//
// Some clients may request direct delivery; the client assumes responsibility
// for correct interpretation of the HID codes.
//
// The geometry of the display and layer are contrained to a 5x5 square. Just
// one view is overlayed on top.
//
//     x - - - -
//     - - - - -
//     - - d - -
//     - - - - -    x - client's view origin
//     - - - - -    d - add and down events, to bring focus to the View.
//
// NOTE: This test is carefully constructed to avoid Vulkan functionality.

namespace lib_ui_input_tests {
namespace {

using InputCommand = fuchsia::ui::input::Command;
using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventType;

// Class fixture for TEST_F. Sets up a 5x5 "display" for GfxSystem.
class HardKeyboardDeliveryTest : public InputSystemTest {
 protected:
  uint32_t test_display_width_px() const override { return 5; }
  uint32_t test_display_height_px() const override { return 5; }
};

TEST_F(HardKeyboardDeliveryTest, InputsGetCorrectlyDelivered) {
  auto [v_token, vh_token] = scenic::ViewTokenPair::New();

  // Set up a scene with one view.
  auto [root_session, root_resources] = CreateScene();
  {
    scenic::Session* const session = root_session.session();

    // Attach the view holder.
    scenic::ViewHolder view_holder(session, std::move(vh_token), "View Holder");
    view_holder.SetViewProperties(k5x5x1);
    root_resources.scene.AddChild(view_holder);

    RequestToPresent(session);
  };

  SessionWrapper client = CreateClient("View", std::move(v_token));

  const uint32_t compositor_id = root_resources.compositor.id();

  // Scene is now set up; send in the input.
  {
    scenic::Session* const session = root_session.session();

    PointerCommandGenerator pointer(root_resources.compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2.5,2.5) location of the 5x5 display.
    // We do enough to trigger a focus change to the View.
    session->Enqueue(pointer.Add(2.5, 2.5));
    session->Enqueue(pointer.Down(2.5, 2.5));

    // The character 'a', pressed and released.
    KeyboardCommandGenerator keyboard(compositor_id, /*device id*/ 2);
    static constexpr uint32_t hid_usage = 0x4;
    static constexpr uint32_t modifiers = 0x0;
    session->Enqueue(keyboard.Pressed(hid_usage, modifiers));
    session->Enqueue(keyboard.Released(hid_usage, modifiers));
  }
  RunLoopUntilIdle();

  // Verify client's inputs do *not* include keyboard events.
  {
    const std::vector<InputEvent>& events = client.events();

    EXPECT_EQ(events.size(), 3u) << "Should receive exactly 3 input events.";

    // ADD
    {
      EXPECT_TRUE(events[0].is_pointer());
      const PointerEvent& add = events[0].pointer();
      EXPECT_EQ(add.x, 2.5);
      EXPECT_EQ(add.y, 2.5);
    }

    // FOCUS
    EXPECT_TRUE(events[1].is_focus());

    // DOWN
    {
      EXPECT_TRUE(events[2].is_pointer());
      const PointerEvent& down = events[2].pointer();
      EXPECT_EQ(down.x, 2.5);
      EXPECT_EQ(down.y, 2.5);
    }
  }

  client.events().clear();

  // Client requests hard keyboard event delivery.
  {
    InputCommand input_cmd;
    input_cmd.set_set_hard_keyboard_delivery({.delivery_request = true});

    client.session()->Enqueue(std::move(input_cmd));
  }
  RunLoopUntilIdle();

  // Send in the input.
  {
    scenic::Session* const session = root_session.session();

    // Client is already in focus, no need to focus again.
    KeyboardCommandGenerator keyboard(compositor_id, /*device id*/ 2);
    static constexpr uint32_t hid_usage = 0x4;
    static constexpr uint32_t modifiers = 0x0;
    session->Enqueue(keyboard.Pressed(hid_usage, modifiers));
    session->Enqueue(keyboard.Released(hid_usage, modifiers));
  }
  RunLoopUntilIdle();

  // Verify client's inputs include keyboard events.
  EXPECT_EQ(client.events().size(), 2u) << "Should receive exactly 2 input events.";
}

// Sets up a session, receives keyboard input, then kills the session, creates a new one and does it
// again. Check that nothing crashes.
TEST_F(HardKeyboardDeliveryTest, SessionDeathCleanupTest) {
  auto [v_token1, vh_token1] = scenic::ViewTokenPair::New();
  auto [v_token2, vh_token2] = scenic::ViewTokenPair::New();

  // Set up a scene with one view.
  auto [root_session, root_resources] = CreateScene();
  {
    scenic::Session* const session = root_session.session();

    // Attach the view holder.
    scenic::ViewHolder view_holder1(session, std::move(vh_token1), "View Holder1");
    scenic::ViewHolder view_holder2(session, std::move(vh_token2), "View Holder2");
    view_holder1.SetViewProperties(k5x5x1);
    view_holder2.SetViewProperties(k5x5x1);
    root_resources.scene.AddChild(view_holder1);
    root_resources.scene.AddChild(view_holder2);

    RequestToPresent(session);
  };

  {
    SessionWrapper client = CreateClient("View", std::move(v_token1));
    {  // Client requests hard keyboard event delivery.
      InputCommand input_cmd;
      input_cmd.set_set_hard_keyboard_delivery({.delivery_request = true});

      client.session()->Enqueue(std::move(input_cmd));
    }

    const uint32_t compositor_id = root_resources.compositor.id();

    // Scene is now set up; send in the input.
    {
      scenic::Session* const session = root_session.session();

      PointerCommandGenerator pointer(root_resources.compositor.id(), /*device id*/ 1,
                                      /*pointer id*/ 1, PointerEventType::TOUCH);
      // A touch sequence that starts at the (2,2) location of the 5x5 display.
      // We do enough to trigger a focus change to the View.
      session->Enqueue(pointer.Add(2, 2));
      session->Enqueue(pointer.Down(2, 2));

      // The character 'a', pressed and released.
      KeyboardCommandGenerator keyboard(compositor_id, /*device id*/ 2);
      static constexpr uint32_t hid_usage = 0x4;
      static constexpr uint32_t modifiers = 0x0;
      session->Enqueue(keyboard.Pressed(hid_usage, modifiers));
      session->Enqueue(keyboard.Released(hid_usage, modifiers));
    }
    RunLoopUntilIdle();
  }

  {
    SessionWrapper client = CreateClient("View", std::move(v_token2));
    {  // Client requests hard keyboard event delivery.
      InputCommand input_cmd;
      // Previously when this command got dispatched Scenic would crash.
      input_cmd.set_set_hard_keyboard_delivery({.delivery_request = true});
      client.session()->Enqueue(std::move(input_cmd));
    }

    const uint32_t compositor_id = root_resources.compositor.id();

    // Scene is now set up; send in the input.
    {
      scenic::Session* const session = root_session.session();

      PointerCommandGenerator pointer(root_resources.compositor.id(), /*device id*/ 1,
                                      /*pointer id*/ 1, PointerEventType::TOUCH);
      // A touch sequence that starts at the (2,2) location of the 5x5 display.
      // We do enough to trigger a focus change to the View.
      session->Enqueue(pointer.Add(2, 2));
      session->Enqueue(pointer.Down(2, 2));

      // The character 'a', pressed and released.
      KeyboardCommandGenerator keyboard(compositor_id, /*device id*/ 2);
      static constexpr uint32_t hid_usage = 0x4;
      static constexpr uint32_t modifiers = 0x0;
      session->Enqueue(keyboard.Pressed(hid_usage, modifiers));
      session->Enqueue(keyboard.Released(hid_usage, modifiers));
    }
    RunLoopUntilIdle();
  }
}

}  // namespace
}  // namespace lib_ui_input_tests
