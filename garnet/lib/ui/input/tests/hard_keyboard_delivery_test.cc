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

using InputCommand = fuchsia::ui::input::Command;
using ScenicEvent = fuchsia::ui::scenic::Event;
using escher::impl::CommandBufferSequencer;
using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventType;
using fuchsia::ui::scenic::SessionListener;
using scenic_impl::Scenic;
using scenic_impl::gfx::Display;
using scenic_impl::gfx::DisplayManager;
using scenic_impl::gfx::test::GfxSystemForTest;
using scenic_impl::input::InputSystem;
using scenic_impl::test::ScenicTest;

// Class fixture for TEST_F. Sets up a 5x5 "display" for GfxSystem.
class HardKeyboardDeliveryTest : public InputSystemTest {
 protected:
  uint32_t test_display_width_px() const override { return 5; }
  uint32_t test_display_height_px() const override { return 5; }
};

class KeyboardSessionWrapper : public SessionWrapper {
 public:
  KeyboardSessionWrapper(scenic_impl::Scenic* scenic)
      : SessionWrapper(scenic) {}
  ~KeyboardSessionWrapper() = default;

  void ClearEvents() { events_.clear(); }
};

TEST_F(HardKeyboardDeliveryTest, Test) {
  KeyboardSessionWrapper presenter(scenic());

  zx::eventpair v_token, vh_token;
  CreateTokenPair(&v_token, &vh_token);

  // Tie the test's dispatcher clock to the system (real) clock.
  RunLoopUntil(zx::clock::get_monotonic());

  // "Presenter" sets up a scene with one view.
  uint32_t compositor_id = 0;
  presenter.RunNow([this, &compositor_id, vh_token = std::move(vh_token)](
                       scenic::Session* session,
                       scenic::EntityNode* root_node) mutable {
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

    // Add local root node to the scene, attach the view holder.
    scene.AddChild(*root_node);
    scenic::ViewHolder view_holder(session, std::move(vh_token), "View Holder");
    root_node->Attach(view_holder);

    RequestToPresent(session);
  });

  // Client sets up its content.
  KeyboardSessionWrapper client(scenic());
  client.RunNow(
      [this, v_token = std::move(v_token)](
          scenic::Session* session, scenic::EntityNode* root_node) mutable {
        // Connect our root node to the presenter's root node.
        scenic::View view(session, std::move(v_token), "View");
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

  // Scene is now set up, send in the input.
  presenter.RunNow([this, compositor_id](scenic::Session* session,
                                         scenic::EntityNode* root_node) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2,2) location of the 5x5 display.
    // We do enough to trigger a focus change to the View.
    session->Enqueue(pointer.Add(2, 2));
    session->Enqueue(pointer.Down(2, 2));

    // The character 'a', pressed and released.
    KeyboardCommandGenerator keyboard(compositor_id, /*device id*/ 2);
    uint32_t hid_usage = 0x4;
    uint32_t modifiers = 0x0;
    session->Enqueue(keyboard.Pressed(hid_usage, modifiers));
    session->Enqueue(keyboard.Released(hid_usage, modifiers));

    RunLoopUntilIdle();
#if 0
    FXL_LOG(INFO) << DumpScenes();  // Handy debugging.
#endif
  });

  // Verify client's inputs do *not* include keyboard events.
  client.ExamineEvents([](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 3u) << "Should receive exactly 3 input events.";

    // ADD
    {
      EXPECT_TRUE(events[0].is_pointer());
      const PointerEvent& add = events[0].pointer();
      EXPECT_EQ(add.x, 2);
      EXPECT_EQ(add.y, 2);
    }

    // FOCUS
    EXPECT_TRUE(events[1].is_focus());

    // DOWN
    {
      EXPECT_TRUE(events[2].is_pointer());
      const PointerEvent& down = events[2].pointer();
      EXPECT_EQ(down.x, 2);
      EXPECT_EQ(down.y, 2);
    }
  });

  client.ClearEvents();

  // Client requests hard keyboard event delivery.
  client.RunNow(
      [this](scenic::Session* session, scenic::EntityNode* root_node) {
        fuchsia::ui::input::SetHardKeyboardDeliveryCmd cmd;
        cmd.delivery_request = true;

        InputCommand input_cmd;
        input_cmd.set_set_hard_keyboard_delivery(std::move(cmd));

        session->Enqueue(std::move(input_cmd));

        RunLoopUntilIdle();
      });

  // Send in the input.
  presenter.RunNow([this, compositor_id](scenic::Session* session,
                                         scenic::EntityNode* root_node) {
    // Client is already in focus, no need to focus again.
    KeyboardCommandGenerator keyboard(compositor_id, /*device id*/ 2);
    uint32_t hid_usage = 0x4;
    uint32_t modifiers = 0x0;
    session->Enqueue(keyboard.Pressed(hid_usage, modifiers));
    session->Enqueue(keyboard.Released(hid_usage, modifiers));

    RunLoopUntilIdle();
  });

  // Verify client's inputs include keyboard events.
  client.ExamineEvents([](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 2u) << "Should receive exactly 2 input events.";
  });
}

}  // namespace lib_ui_input_tests
