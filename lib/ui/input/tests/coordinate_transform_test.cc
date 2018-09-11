// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async/cpp/time.h>
#include <lib/zx/eventpair.h>

#include "garnet/lib/ui/gfx/displays/display.h"
#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/gfx/id.h"
#include "garnet/lib/ui/gfx/tests/mocks.h"
#include "garnet/lib/ui/input/input_system.h"
#include "garnet/lib/ui/input/tests/util.h"
#include "garnet/lib/ui/scenic/scenic.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/escher/impl/command_buffer_sequencer.h"
#include "lib/fxl/logging.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/ui/gfx/tests/gfx_test.h"
#include "lib/ui/input/cpp/formatting.h"
#include "lib/ui/scenic/command_dispatcher.h"
#include "lib/ui/scenic/cpp/commands.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"

// This test exercises the coordinate transform logic applied to pointer events
// sent to each client. We set up a scene with two translated but overlapping
// Views, and see if events are conveyed to the client in an appropriate way.
//
// The geometry is constrained to a 9x9 display and layer, with two 5x5
// rectangles that intersect in one pixel, like so:
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
// To create this test setup, we perform translation of each View itself (i.e.,
// (0,0) and (4,4)), in addition to aligning (translating) each View's Shape to
// its owning View.
//
// Session 1 creates its rectangle in the upper left quadrant; the View's origin
// is marked 'x'. Similarly, session 2 creates its rectangle in the bottom right
// quadrant; the View's origin marked 'y'.
//
// The hit test occurs at the center of the screen (colocated with View 2's
// origin at 'y'), at (4,4) in device space. The touch events move diagonally up
// and to the right, and we have the following correspondence of coordinates:
//
// Event  Mark  Device  View-1  View-2
// ADD    y     (4,4)   (4,4)   (0, 0)
// DOWN   y     (4,4)   (4,4)   (0, 0)
// MOVE   M     (5,3)   (5,3)   (1,-1)
// UP     U     (6,2)   (6,2)   (2,-2)
// REMOVE U     (6,2)   (6,2)   (2,-2)
//
// N.B. Session 1 sits *above* session 2 in elevation; hence, session 1 should
// receive the focus event.
//
// N.B. This test is carefully constructed to avoid Vulkan functionality.

namespace lib_ui_input_tests {

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

// Device-independent "display"; for testing only. Needed to ensure GfxSystem
// doesn't wait for a device-driven "display ready" signal.
class TestDisplay : public Display {
 public:
  static uint32_t kWidth;
  static uint32_t kHeight;

  TestDisplay(uint64_t id) : Display(id, kWidth, kHeight) {}
  ~TestDisplay() = default;
  bool is_test_display() const override { return true; }
};

// Test-global display defaults.
uint32_t TestDisplay::kWidth = 9;
uint32_t TestDisplay::kHeight = 9;

// Class fixture for TEST_F. Sets up a 9x9 "display" for GfxSystem to use, as
// well as a live InputSystem to test.
class InputSystemTest : public ScenicTest {
 public:
  Scenic* scenic() { return scenic_.get(); }

  std::string DumpScenes() { return gfx_->engine()->DumpScenes(); }

  // Convenience function; triggers scene operations.
  void RequestToPresent(scenic::Session* session) {
    bool scene_presented = false;
    session->Present(
        /*presentation time*/ 0,
        [&scene_presented](fuchsia::images::PresentationInfo info) {
          scene_presented = true;
        });
    RunLoopFor(zx::msec(20));  // Schedule the render task.
    EXPECT_TRUE(scene_presented);
  }

 protected:
  void TearDown() override {
    // A clean teardown sequence is a little involved but possible.
    // 0. Sessions Flush their last resource-release cmds (in ~SessionWrapper).
    // 1. Scenic runs the last resource-release cmds.
    RunLoopUntilIdle();
    // 2. Destroy Scenic before destroying the command buffer sequencer (CBS).
    //    This ensures no CBS listeners are active by the time CBS is destroyed.
    //    Scenic is destroyed by the superclass TearDown (now), CBS is destroyed
    //    by the implicit class destructor (later).
    ScenicTest::TearDown();
  }

  void InitializeScenic(Scenic* scenic) override {
    auto display_manager = std::make_unique<DisplayManager>();
    display_manager->SetDefaultDisplayForTests(
        std::make_unique<TestDisplay>(/*id*/ 0));
    command_buffer_sequencer_ = std::make_unique<CommandBufferSequencer>();
    gfx_ = scenic->RegisterSystem<GfxSystemForTest>(
        std::move(display_manager), command_buffer_sequencer_.get());
    input_ = scenic->RegisterSystem<InputSystem>(gfx_);
  }

 private:
  std::unique_ptr<CommandBufferSequencer> command_buffer_sequencer_;
  GfxSystemForTest* gfx_ = nullptr;
  InputSystem* input_ = nullptr;
};

// Convenience wrapper to write Scenic clients with less boilerplate.
class SessionWrapper {
 public:
  SessionWrapper(Scenic* scenic) : scenic_(scenic) {
    fuchsia::ui::scenic::SessionPtr session_fidl;
    fidl::InterfaceHandle<SessionListener> listener_handle;
    fidl::InterfaceRequest<SessionListener> listener_request =
        listener_handle.NewRequest();
    scenic_->CreateSession(session_fidl.NewRequest(),
                           std::move(listener_handle));
    session_ = std::make_unique<scenic::Session>(std::move(session_fidl),
                                                 std::move(listener_request));
    root_node_ = std::make_unique<scenic::EntityNode>(session_.get());

    session_->set_event_handler([this](fidl::VectorPtr<ScenicEvent> events) {
      for (ScenicEvent& event : *events) {
        if (event.is_input()) {
          events_.push_back(std::move(event.input()));
        }
        // Ignore other event types for this test.
      }
    });
  }

  ~SessionWrapper() {
    root_node_.reset();  // Let go of the resource; enqueue the release cmd.
    session_->Flush();   // Ensure Scenic receives the release cmd.
  }

  void RunNow(fit::function<void(scenic::Session* session,
                                 scenic::EntityNode* root_node)>
                  create_scene_callback) {
    create_scene_callback(session_.get(), root_node_.get());
  }

  void ExamineEvents(
      fit::function<void(const std::vector<InputEvent>& events)>
          examine_events_callback) {
    examine_events_callback(events_);
  }

 private:
  scenic_impl::Scenic* const scenic_;
  std::unique_ptr<scenic::Session> session_;
  std::unique_ptr<scenic::EntityNode> root_node_;
  std::vector<InputEvent> events_;
};

// Convenience function to reduce clutter.
void CreateTokenPairs(zx::eventpair* t1, zx::eventpair* t2) {
  zx_status_t status = zx::eventpair::create(/*flags*/ 0u, t1, t2);
  FXL_CHECK(status == ZX_OK);
}

TEST_F(InputSystemTest, CoordinateTransform) {
  SessionWrapper presenter(scenic());

  zx::eventpair vh1, v1, vh2, v2;
  CreateTokenPairs(&vh1, &v1);
  CreateTokenPairs(&vh2, &v2);

  // Tie the test's dispatcher clock to the system (real) clock.
  RunLoopUntil(zx::clock::get_monotonic());

  // "Presenter" sets up a scene with two ViewHolders.
  uint32_t compositor_id = 0;
  presenter.RunNow([this, &compositor_id, vh1 = std::move(vh1),
                  vh2 = std::move(vh2)](scenic::Session* session,
                                        scenic::EntityNode* root_node) mutable {
    // Minimal scene.
    scenic::Compositor compositor(session);
    compositor_id = compositor.id();

    scenic::Scene scene(session);
    scenic::Camera camera(scene);
    scenic::Renderer renderer(session);
    renderer.SetCamera(camera);

    scenic::Layer layer(session);
    layer.SetSize(TestDisplay::kWidth, TestDisplay::kHeight);
    layer.SetRenderer(renderer);

    scenic::LayerStack layer_stack(session);
    layer_stack.AddLayer(layer);
    compositor.SetLayerStack(layer_stack);

    // Add local root node to the scene. Attach two entity nodes that perform
    // translation for the two clients; attach ViewHolders.
    scene.AddChild(*root_node);
    scenic::EntityNode translate_1(session), translate_2(session);
    scenic::ViewHolder holder_1(session, std::move(vh1), "holder_1"),
        holder_2(session, std::move(vh2), "holder_2");

    root_node->AddChild(translate_1);
    translate_1.SetTranslation(0, 0, 2);
    translate_1.Attach(holder_1);

    root_node->AddChild(translate_2);
    translate_2.SetTranslation(4, 4, 1);
    translate_2.Attach(holder_2);

    RequestToPresent(session);
  });

  // Client 1 vends a View to the global scene.
  SessionWrapper client_1(scenic());
  client_1.RunNow(
      [this, v1 = std::move(v1)](scenic::Session* session,
                                 scenic::EntityNode* root_node) mutable {
        scenic::View view_1(session, std::move(v1), "view_1");
        view_1.AddChild(*root_node);

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
      [this, v2 = std::move(v2)](scenic::Session* session,
                                 scenic::EntityNode* root_node) mutable {
        scenic::View view_2(session, std::move(v2), "view_2");
        view_2.AddChild(*root_node);

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
    PointerEventGenerator pointer(compositor_id, /*device id*/ 1,
                                  /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts in the direct center of the 9x9 display.
    // The sequence ends 2x2 diagonally away (north-east) from the touch down.
    session->Enqueue(pointer.Add(4, 4));
    session->Enqueue(pointer.Down(4, 4));
    session->Enqueue(pointer.Move(5, 3));
    session->Enqueue(pointer.Up(6, 2));
    session->Enqueue(pointer.Remove(6, 2));
    RunLoopUntilIdle();

#if 0
    FXL_LOG(INFO) << DumpScenes();  // Handy debugging.
#endif
  });

  client_1.ExamineEvents([this](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 6u) << "Should receive exactly 6 input events.";

    // ADD
    {
      EXPECT_TRUE(events[0].is_pointer());
      const PointerEvent& add = events[0].pointer();
      EXPECT_EQ(add.x, 4);
      EXPECT_EQ(add.y, 4);
    }

    // FOCUS
    EXPECT_TRUE(events[1].is_focus());

    // DOWN
    {
      EXPECT_TRUE(events[2].is_pointer());
      const PointerEvent& down = events[2].pointer();
      EXPECT_EQ(down.x, 4);
      EXPECT_EQ(down.y, 4);
    }
    // MOVE
    {
      EXPECT_TRUE(events[3].is_pointer());
      const PointerEvent& move = events[3].pointer();
      EXPECT_EQ(move.x, 5);
      EXPECT_EQ(move.y, 3);
    }
    // UP
    {
      EXPECT_TRUE(events[4].is_pointer());
      const PointerEvent& up = events[4].pointer();
      EXPECT_EQ(up.x, 6);
      EXPECT_EQ(up.y, 2);
    }
    // REMOVE
    {
      EXPECT_TRUE(events[5].is_pointer());
      const PointerEvent& remove = events[5].pointer();
      EXPECT_EQ(remove.x, 6);
      EXPECT_EQ(remove.y, 2);
    }
  });

  client_2.ExamineEvents([this](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 5u) << "Should receive exactly 5 input events.";

    // ADD
    {
      const InputEvent& add = events[0];
      EXPECT_TRUE(add.is_pointer());
      EXPECT_EQ(add.pointer().x, 0);
      EXPECT_EQ(add.pointer().y, 0);
    }

    // DOWN
    {
      EXPECT_TRUE(events[1].is_pointer());
      const PointerEvent& down = events[1].pointer();
      EXPECT_EQ(down.x, 0);
      EXPECT_EQ(down.y, 0);
    }
    // MOVE
    {
      EXPECT_TRUE(events[2].is_pointer());
      const PointerEvent& move = events[2].pointer();
      EXPECT_EQ(move.x, 1);
      EXPECT_EQ(move.y, -1);
    }
    // UP
    {
      EXPECT_TRUE(events[3].is_pointer());
      const PointerEvent& up = events[3].pointer();
      EXPECT_EQ(up.x, 2);
      EXPECT_EQ(up.y, -2);
    }
    // REMOVE
    {
      EXPECT_TRUE(events[4].is_pointer());
      const PointerEvent& remove = events[4].pointer();
      EXPECT_EQ(remove.x, 2);
      EXPECT_EQ(remove.y, -2);
    }

#if 0
    for (const auto& event : events)
      FXL_LOG(INFO) << "Client got: " << event;  // Handy debugging.
#endif
  });
}

}  // namespace lib_ui_input_tests
