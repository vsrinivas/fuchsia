// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/input/tests/util.h"

// These tests exercise InputSystem logic during startup, e.g. potential race conditions.

namespace lib_ui_input_tests {
namespace {

using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;

// Class fixture for TEST_F. Sets up a 5x5 "display" for GfxSystem.
class StartupTest : public InputSystemTest {
 protected:
  uint32_t test_display_width_px() const override { return 5; }
  uint32_t test_display_height_px() const override { return 5; }

  // Injects an arbitrary input event using the legacy injection API.
  // Uses a new pointer on each injection to minimize conteracting between different injections.
  void InjectFreshEvent(scenic::Session* session, uint32_t compositor_id) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ ++last_pointer_id_, PointerEventType::TOUCH);
    session->Enqueue(pointer.Add(2.5, 2.5));
  }

  uint32_t last_pointer_id_ = 0;
};

// This test builds up a scene piece by piece, injecting input at every point to confirm
// that there is no crash.
TEST_F(StartupTest, LegacyInjectBeforeSceneSetupComplete_ShouldNotCrash) {
  static uint32_t kFakeCompositorId = 321241;
  SessionWrapper root_session(scenic());
  scenic::Session* const session = root_session.session();
  auto [v, vh] = scenic::ViewTokenPair::New();
  SessionWrapper client = CreateClient("view", std::move(v));
  scenic::ViewHolder holder(session, std::move(vh), "holder");
  holder.SetViewProperties(k5x5x1);

  // Empty.
  RequestToPresent(session);
  InjectFreshEvent(session, kFakeCompositorId);
  RunLoopUntilIdle();  // Should not crash.
  EXPECT_TRUE(client.events().empty());

  // Only a Scene object.
  scenic::Scene scene(session);
  RequestToPresent(session);
  InjectFreshEvent(session, kFakeCompositorId);
  RunLoopUntilIdle();  // Should not crash.
  EXPECT_TRUE(client.events().empty());

  // Attach the child to the scene now that we have one.
  scene.AddChild(holder);

  scenic::Camera camera(scene);
  RequestToPresent(session);
  InjectFreshEvent(session, kFakeCompositorId);
  RunLoopUntilIdle();  // Should not crash.
  EXPECT_TRUE(client.events().empty());

  scenic::Renderer renderer(session);
  RequestToPresent(session);
  InjectFreshEvent(session, kFakeCompositorId);
  RunLoopUntilIdle();  // Should not crash.
  EXPECT_TRUE(client.events().empty());

  renderer.SetCamera(camera);
  RequestToPresent(session);
  InjectFreshEvent(session, kFakeCompositorId);
  RunLoopUntilIdle();  // Should not crash.
  EXPECT_TRUE(client.events().empty());

  scenic::Compositor compositor(session);
  RequestToPresent(session);
  const uint32_t compositor_id = compositor.id();
  InjectFreshEvent(session, kFakeCompositorId);  // With fake compositor id.
  InjectFreshEvent(session, compositor_id);      // With real compositor id.
  RunLoopUntilIdle();                            // Should not crash.

  scenic::LayerStack layer_stack(session);
  RequestToPresent(session);
  InjectFreshEvent(session, compositor_id);
  RunLoopUntilIdle();  // Should not crash.
  EXPECT_TRUE(client.events().empty());

  compositor.SetLayerStack(layer_stack);
  RequestToPresent(session);
  InjectFreshEvent(session, compositor_id);
  RunLoopUntilIdle();  // Should not crash.
  EXPECT_TRUE(client.events().empty());

  scenic::Layer layer(session);
  RequestToPresent(session);
  InjectFreshEvent(session, compositor_id);
  RunLoopUntilIdle();  // Should not crash.
  EXPECT_TRUE(client.events().empty());

  layer_stack.AddLayer(layer);
  RequestToPresent(session);
  InjectFreshEvent(session, compositor_id);
  RunLoopUntilIdle();  // Should not crash.
  EXPECT_TRUE(client.events().empty());

  layer.SetRenderer(renderer);
  RequestToPresent(session);
  InjectFreshEvent(session, compositor_id);
  RunLoopUntilIdle();  // Should not crash.
  EXPECT_TRUE(client.events().empty());

  layer.SetSize(10, 10);
  RequestToPresent(session);
  InjectFreshEvent(session, compositor_id);
  RunLoopUntilIdle();  // Should not crash.

  // Should now have received the final event.
  EXPECT_FALSE(client.events().empty());
}

}  // namespace
}  // namespace lib_ui_input_tests
