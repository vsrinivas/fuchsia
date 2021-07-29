// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/ui/lib/glm_workaround/glm_workaround.h"
#include "src/ui/scenic/integration_tests/utils.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>

// These tests exercise the Screen Space to View Space coordinate transform logic applied to
// pointer events sent to sessions.
// Setup:
// Injection done in screen space, with fuchsia.ui.input.Command (legacy)
// Target(s) specified with hit test
// Dispatch done in fuchsia.ui.scenic.SessionListener (legacy)

namespace integration_tests {

using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;
static constexpr fuchsia::ui::gfx::ViewProperties k5x5x1 = {.bounding_box = {.max = {5, 5, 1}}};

const std::map<std::string, std::string> LocalServices() {
  return {{"fuchsia.ui.composition.Allocator",
           "fuchsia-pkg://fuchsia.com/gfx_integration_tests#meta/scenic.cmx"},
          {"fuchsia.ui.scenic.Scenic",
           "fuchsia-pkg://fuchsia.com/gfx_integration_tests#meta/scenic.cmx"},
          {"fuchsia.ui.views.ViewRefInstalled",
           "fuchsia-pkg://fuchsia.com/gfx_integration_tests#meta/scenic.cmx"},
          {"fuchsia.hardware.display.Provider",
           "fuchsia-pkg://fuchsia.com/fake-hardware-display-controller-provider#meta/hdcp.cmx"}};
}

// Allow these global services.
const std::vector<std::string> GlobalServices() {
  return {"fuchsia.vulkan.loader.Loader", "fuchsia.sysmem.Allocator"};
}

using fuchsia::ui::views::ViewRef;
using WatchResult = fuchsia::ui::views::ViewRefInstalled_Watch_Result;

std::unique_ptr<scenic::Session> CreateSession(fuchsia::ui::scenic::Scenic* scenic) {
  fuchsia::ui::scenic::SessionEndpoints endpoints;
  fuchsia::ui::scenic::SessionPtr session_ptr;
  fuchsia::ui::scenic::SessionListenerHandle listener_handle;
  auto listener_request = listener_handle.NewRequest();
  endpoints.set_session(session_ptr.NewRequest());
  endpoints.set_session_listener(std::move(listener_handle));
  scenic->CreateSessionT(std::move(endpoints), [] {});

  return std::make_unique<scenic::Session>(std::move(session_ptr), std::move(listener_request));
}

// Sets up the root of a scene.
// Present() must be called separately by the creator, since this does not have access to the
// looper.
struct RootSession {
  RootSession(fuchsia::ui::scenic::Scenic* scenic)
      : session(CreateSession(scenic)),
        compositor(session.get()),
        layer_stack(session.get()),
        layer(session.get()),
        renderer(session.get()),
        scene(session.get()),
        camera(scene) {
    compositor.SetLayerStack(layer_stack);
    layer_stack.AddLayer(layer);
    layer.SetSize(/*width*/ 9, /*height*/ 9);  // 9x9 "display".
    layer.SetRenderer(renderer);
    renderer.SetCamera(camera);
  }

  std::unique_ptr<scenic::Session> session;
  scenic::DisplayCompositor compositor;
  scenic::LayerStack layer_stack;
  scenic::Layer layer;
  scenic::Renderer renderer;
  scenic::Scene scene;
  scenic::Camera camera;

  std::unique_ptr<scenic::ViewHolder> view_holder;
};

// Test fixture that sets up an environment with a Scenic we can connect to.
class GfxLegacyCoordinateTransformTest : public sys::testing::TestWithEnvironment {
 public:
  fuchsia::ui::scenic::Scenic* scenic() { return scenic_.get(); }

  void SetUp() override {
    TestWithEnvironment::SetUp();
    environment_ = CreateNewEnclosingEnvironment("gfx_legacy_coordinate_transform_test_environment",
                                                 CreateServices());
    environment_->ConnectToService(scenic_.NewRequest());
    scenic_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
    });

    // Set up root view.
    root_session_ = std::make_unique<RootSession>(scenic());
    root_session_->session->set_error_handler([](auto) { FAIL() << "Root session terminated."; });
    BlockingPresent(*root_session_->session);

    environment_->ConnectToService(view_ref_installed_ptr_.NewRequest());
  }

  void BlockingPresent(scenic::Session& session) {
    bool presented = false;
    session.set_on_frame_presented_handler([&presented](auto) { presented = true; });
    session.Present2(0, 0, [](auto) {});
    RunLoopUntil([&presented] { return presented; });
    session.set_on_frame_presented_handler([](auto) {});
  }

  std::unique_ptr<scenic::Session> CreateChildView(fuchsia::ui::views::ViewToken view_token,
                                                   std::string debug_name) {
    auto session = CreateSession(scenic());
    scenic::View view(session.get(), std::move(view_token), debug_name);
    scenic::ShapeNode shape(session.get());
    scenic::Rectangle rec(session.get(), 5, 5);
    shape.SetTranslation(2.5f, 2.5f, 0);  // Center the shape within the View.
    view.AddChild(shape);
    shape.SetShape(rec);
    BlockingPresent(*session);

    return session;
  }

  // Configures services available to the test environment. This method is called by |SetUp()|. It
  // shadows but calls |TestWithEnvironment::CreateServices()|.
  std::unique_ptr<sys::testing::EnvironmentServices> CreateServices() {
    auto services = TestWithEnvironment::CreateServices();
    for (const auto& [name, url] : LocalServices()) {
      const zx_status_t is_ok = services->AddServiceWithLaunchInfo({.url = url}, name);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << name;
    }

    for (const auto& service : GlobalServices()) {
      const zx_status_t is_ok = services->AllowParentService(service);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << service;
    }

    return services;
  }

 protected:
  std::unique_ptr<RootSession> root_session_;
  fuchsia::ui::views::ViewRefInstalledPtr view_ref_installed_ptr_;

 private:
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
  fuchsia::ui::scenic::ScenicPtr scenic_;
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
// The hit test occurs at the center of the screen (colocated with View 2's origin at 'y'), at
// (4.5,4.5) in device space. The touch events move diagonally up and to the right, and we have the
// following correspondence of coordinates:
//
// Event  Mark  Device      View-1      View-2
// ADD    y     (4.5,4.5)   N/A         (0.5, 0.5)
// DOWN   y     (4.5,4.5)   N/A         (0.5, 0.5)
// MOVE   M     (5.5,3.5)   N/A         (1.5,-0.5)
// UP     U     (6.5,2.5)   N/A         (2.5,-1.5)
// REMOVE U     (6.5,2.5)   N/A         (2.5,-1.5)
//
// N.B. View 2 sits *above* View 1 in elevation; hence, only View 2 should receive touch events.
//
// N.B. This test is carefully constructed to avoid Vulkan functionality.
TEST_F(GfxLegacyCoordinateTransformTest, Translated) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up a scene with two ViewHolders.
  {
    scenic::Session* const session = root_session_->session.get();
    scenic::Scene& scene = root_session_->scene;

    // Attach two translated ViewHolders.
    scenic::ViewHolder holder_1(session, std::move(vh1), "holder_1"),
        holder_2(session, std::move(vh2), "holder_2");

    holder_1.SetViewProperties(k5x5x1);
    holder_2.SetTranslation(4, 4, -2);  // elevation 2
    holder_2.SetViewProperties(k5x5x1);
    holder_1.SetTranslation(0, 0, -1);  // elevation 1

    scene.AddChild(holder_1);
    scene.AddChild(holder_2);

    BlockingPresent(*session);
  }

  std::unique_ptr<scenic::Session> child1_session = CreateChildView(std::move(v1), "child1_view");
  std::vector<InputEvent> child1_events;
  child1_session->set_event_handler(
      [&child1_events](std::vector<fuchsia::ui::scenic::Event> events) {
        for (auto& event : events) {
          if (event.is_input() && !event.input().is_focus()) {
            child1_events.emplace_back(std::move(event.input()));
          }
        }
      });

  std::unique_ptr<scenic::Session> child2_session = CreateChildView(std::move(v2), "child2_view");
  std::vector<InputEvent> child2_events;
  child2_session->set_event_handler(
      [&child2_events](std::vector<fuchsia::ui::scenic::Event> events) {
        for (auto& event : events) {
          if (event.is_input() && !event.input().is_focus()) {
            child2_events.emplace_back(std::move(event.input()));
          }
        }
      });

  // Multi-agent scene is now set up, send in the input.
  {
    scenic::Session* session = root_session_->session.get();

    PointerCommandGenerator pointer(root_session_->compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts in the direct center of the 9x9 display.
    // The sequence ends 2x2 diagonally away (north-east) from the touch down.
    session->Enqueue(pointer.Add(4.5, 4.5));
    session->Enqueue(pointer.Down(4.5, 4.5));
    session->Enqueue(pointer.Move(5.5, 3.5));
    session->Enqueue(pointer.Up(6.5, 2.5));
    session->Enqueue(pointer.Remove(6.5, 2.5));
  }
  RunLoopUntil([&child2_events] { return child2_events.size() == 5u; });  // Succeeds or times out.

  EXPECT_EQ(child1_events.size(), 0u);  // Occluded and thus excluded.

  EXPECT_EQ(child2_events.size(), 5u);
  ASSERT_TRUE(child2_events[0].is_pointer());
  EXPECT_TRUE(PointerMatches(child2_events[0].pointer(), 1u, PointerEventPhase::ADD, 0.5, 0.5));
  ASSERT_TRUE(child2_events[1].is_pointer());
  EXPECT_TRUE(PointerMatches(child2_events[1].pointer(), 1u, PointerEventPhase::DOWN, 0.5, 0.5));
  ASSERT_TRUE(child2_events[2].is_pointer());
  // TODO(fxbug.dev/81710): The following have coordinates clamped to their owning view.
  EXPECT_TRUE(PointerMatches(child2_events[2].pointer(), 1u, PointerEventPhase::MOVE, 1.5, 0));
  ASSERT_TRUE(child2_events[3].is_pointer());
  EXPECT_TRUE(PointerMatches(child2_events[3].pointer(), 1u, PointerEventPhase::UP, 2.5, 0));
  ASSERT_TRUE(child2_events[4].is_pointer());
  EXPECT_TRUE(PointerMatches(child2_events[4].pointer(), 1u, PointerEventPhase::REMOVE, 2.5, 0));
}

// This test verifies scaling applied to a view subgraph behind another.
// The scaling performed to the "behind" view does not affect coordinates for the "front" view.
TEST_F(GfxLegacyCoordinateTransformTest, ScaledBehind) {
  // v1 is in front, not scaled
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  // v2 is in back but scaled 4x
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up a scene with two ViewHolders.
  {
    scenic::Session* const session = root_session_->session.get();
    scenic::Scene& scene = root_session_->scene;

    scenic::ViewHolder holder_1(session, std::move(vh1), "holder_1"),
        holder_2(session, std::move(vh2), "holder_2");

    holder_1.SetViewProperties(k5x5x1);
    holder_1.SetTranslation(1, 1, -5);
    holder_2.SetViewProperties(k5x5x1);
    holder_2.SetTranslation(1, 1, -4);
    holder_2.SetScale(4, 4, 4);

    scene.AddChild(holder_1);
    scene.AddChild(holder_2);

    BlockingPresent(*session);
  }

  std::unique_ptr<scenic::Session> child1_session = CreateChildView(std::move(v1), "child1_view");
  std::vector<InputEvent> child1_events;
  child1_session->set_event_handler(
      [&child1_events](std::vector<fuchsia::ui::scenic::Event> events) {
        for (auto& event : events) {
          if (event.is_input() && !event.input().is_focus()) {
            child1_events.emplace_back(std::move(event.input()));
          }
        }
      });

  std::unique_ptr<scenic::Session> child2_session = CreateChildView(std::move(v2), "child2_view");
  std::vector<InputEvent> child2_events;
  child2_session->set_event_handler(
      [&child2_events](std::vector<fuchsia::ui::scenic::Event> events) {
        for (auto& event : events) {
          if (event.is_input() && !event.input().is_focus()) {
            child2_events.emplace_back(std::move(event.input()));
          }
        }
      });

  // Multi-agent scene is now set up, send in the input.
  {
    scenic::Session* const session = root_session_->session.get();

    PointerCommandGenerator pointer(root_session_->compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // Touch once at (2.5, 2.5)
    session->Enqueue(pointer.Add(2.5, 2.5));
    session->Enqueue(pointer.Down(2.5, 2.5));
  }
  RunLoopUntil([&child1_events] { return child1_events.size() == 2; });  // Succeeds or times out.

  EXPECT_EQ(child2_events.size(), 0u);  // Occluded and thus excluded.

  ASSERT_EQ(child1_events.size(), 2u);
  ASSERT_TRUE(child1_events[0].is_pointer());
  EXPECT_TRUE(PointerMatches(child1_events[0].pointer(), 1u, PointerEventPhase::ADD, 1.5, 1.5));
  ASSERT_TRUE(child1_events[1].is_pointer());
  EXPECT_TRUE(PointerMatches(child1_events[1].pointer(), 1u, PointerEventPhase::DOWN, 1.5, 1.5));
}

// This test verifies scaling applied to a view subgraph in front of another.
// The scaling performed to the "front" view ought to be observable.
TEST_F(GfxLegacyCoordinateTransformTest, ScaledInFront) {
  // v1 is in front and scaled 4x
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  // v2 is in back but not scaled
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up a scene with two ViewHolders.
  {
    scenic::Session* const session = root_session_->session.get();
    scenic::Scene& scene = root_session_->scene;

    scenic::ViewHolder holder_1(session, std::move(vh1), "holder_1"),
        holder_2(session, std::move(vh2), "holder_2");

    holder_1.SetViewProperties(k5x5x1);
    holder_1.SetTranslation(1, 1, -5);
    holder_1.SetScale(4, 4, 4);
    holder_2.SetViewProperties(k5x5x1);
    holder_2.SetTranslation(1, 1, -1);

    scene.AddChild(holder_1);
    scene.AddChild(holder_2);

    BlockingPresent(*session);
  }

  std::unique_ptr<scenic::Session> child1_session = CreateChildView(std::move(v1), "child1_view");
  std::vector<InputEvent> child1_events;
  child1_session->set_event_handler(
      [&child1_events](std::vector<fuchsia::ui::scenic::Event> events) {
        for (auto& event : events) {
          if (event.is_input() && !event.input().is_focus()) {
            child1_events.emplace_back(std::move(event.input()));
          }
        }
      });

  std::unique_ptr<scenic::Session> child2_session = CreateChildView(std::move(v2), "child2_view");
  std::vector<InputEvent> child2_events;
  child2_session->set_event_handler(
      [&child2_events](std::vector<fuchsia::ui::scenic::Event> events) {
        for (auto& event : events) {
          if (event.is_input() && !event.input().is_focus()) {
            child2_events.emplace_back(std::move(event.input()));
          }
        }
      });

  // Multi-agent scene is now set up, send in the input.
  {
    scenic::Session* session = root_session_->session.get();

    PointerCommandGenerator pointer(root_session_->compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // Touch once at (2.5, 2.5)
    session->Enqueue(pointer.Add(2.5, 2.5));
    session->Enqueue(pointer.Down(2.5, 2.5));
  }
  RunLoopUntil([&child1_events] { return child1_events.size() == 2u; });

  EXPECT_EQ(child2_events.size(), 0u);  // Occluded and thus excluded.

  ASSERT_EQ(child1_events.size(), 2u);
  ASSERT_TRUE(child1_events[0].is_pointer());
  EXPECT_TRUE(
      PointerMatches(child1_events[0].pointer(), 1u, PointerEventPhase::ADD, 1.5 / 4, 1.5 / 4));
  ASSERT_TRUE(child1_events[1].is_pointer());
  EXPECT_TRUE(
      PointerMatches(child1_events[1].pointer(), 1u, PointerEventPhase::DOWN, 1.5 / 4, 1.5 / 4));
}

// This test verifies that rotation is handled correctly when events are delivered to clients.
//
// Below are ASCII diagrams showing the scene setup.
// Each character is a point on a surface, the top left point representing (0,0)
// and the bottom right (5,5).
// Note that the notated X,Y coordinate system is the screen coordinate system. The view's
// coordinate system has its origin at corner '1'.
//
// View pre-transformation (1,2,3,4 denote corners of view):
// Note that the view's coordinate system is the same as the screen coordinate system.
//   X ->
// Y 1 O O O O 2 - - - -
// | O O O O O O - - - -
// v O O O O O O - - - -
//   O O O O O O - - - -
//   O O O O O O - - - -
//   4 O O O O 3 - - - -
//   - - - - - - - - - -
//   - - - - - - - - - -
//   - - - - - - - - - -
//   - - - - - - - - - -
//
// View post-transformation:
//   X ->
// Y 4A O O O O 1D- - - -
// | O  O O O O O - - - -
// V O  O O O O O - - - -
//   O  O O O O O - - - -
//   O  O O O O O - - - -
//   3U O O O O 2M- - - -
//   -  - - - - - - - - -
//   -  - - - - - - - - -
//   -  - - - - - - - - -
//   -  - - - - - - - - -
//
// A - Add event
// D - Down event
// M - Move event
// U - Up event

TEST_F(GfxLegacyCoordinateTransformTest, Rotated) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  {
    scenic::Session* const session = root_session_->session.get();
    scenic::Scene& scene = root_session_->scene;

    scenic::ViewHolder view_holder(session, std::move(view_holder_token), "view_holder");

    view_holder.SetViewProperties(k5x5x1);
    scene.AddChild(view_holder);

    // Rotate the view holder 90 degrees counter-clockwise around the z-axis (which points into
    // screen, so the rotation appears clockwise).
    view_holder.SetAnchor(2.5, 2.5, 0);
    const auto rotation_quaternion = glm::angleAxis(glm::pi<float>() / 2, glm::vec3(0, 0, 1));
    view_holder.SetRotation(rotation_quaternion.x, rotation_quaternion.y, rotation_quaternion.z,
                            rotation_quaternion.w);

    BlockingPresent(*session);
  }

  // Client vends a View to the global scene.
  std::unique_ptr<scenic::Session> child1_session =
      CreateChildView(std::move(view_token), "child1_view");
  std::vector<InputEvent> child1_events;
  child1_session->set_event_handler(
      [&child1_events](std::vector<fuchsia::ui::scenic::Event> events) {
        for (auto& event : events) {
          if (event.is_input() && !event.input().is_focus()) {
            child1_events.emplace_back(std::move(event.input()));
          }
        }
      });

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation. Injected events are in the global coordinate space.
  {
    scenic::Session* const session = root_session_->session.get();

    PointerCommandGenerator pointer(root_session_->compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Add(0.5, 0.5));
    session->Enqueue(pointer.Down(4.5, 0.5));
    session->Enqueue(pointer.Move(4.5, 4.5));
    session->Enqueue(pointer.Up(0.5, 4.5));
  }
  RunLoopUntil([&child1_events] { return child1_events.size() == 4u; });  // Succeeds or times out.

  EXPECT_EQ(child1_events.size(), 4u);
  EXPECT_TRUE(child1_events[0].is_pointer());
  EXPECT_TRUE(PointerMatches(child1_events[0].pointer(), 1u, PointerEventPhase::ADD, 0.5, 4.5));
  EXPECT_TRUE(child1_events[1].is_pointer());
  EXPECT_TRUE(PointerMatches(child1_events[1].pointer(), 1u, PointerEventPhase::DOWN, 0.5, 0.5));
  EXPECT_TRUE(child1_events[2].is_pointer());
  EXPECT_TRUE(PointerMatches(child1_events[2].pointer(), 1u, PointerEventPhase::MOVE, 4.5, 0.5));
  EXPECT_TRUE(child1_events[3].is_pointer());
  EXPECT_TRUE(PointerMatches(child1_events[3].pointer(), 1u, PointerEventPhase::UP, 4.5, 4.5));
}

// In this test we set up a view, apply a ClipSpaceTransform to it, and then send pointer events to
// confirm that the coordinates received by the session are correctly transformed.
//
// Below are ASCII diagrams showing the scene setup.
// Each character is a point on a surface, the top left point representing (0,0)
// and the bottom right (5,5).
// Note that the notated X,Y coordinate system is the screen coordinate system. The view's
// coordinate system has its origin at corner '1'.
//
// Scene pre-transformation (1,2,3,4 denote the corners of the view):
// Note that the view's coordinate system is the same as the screen coordinate system.
//   X ->
// Y 1 O O O O 2 - - - -
// | O O O O O O - - - -
// v O O O O O O - - - -
//   O O O O O O - - - -
//   O O O O O O - - - -
//   4 O O O O 3 - - - -
//   - - - - - - - - - -
//   - - - - - - - - - -
//   - - - - - - - - - -
//   - - - - - - - - - -
//
// Scene after scale, before offset:
// 1   O   O   O   O   2
//
// O   O   O   O   O   O
//
// O   O   O - O - O - O - - -
//         - - - - - - - - - -
// O   O   O - O - O - O - - -
//         - - - - - - - - - -
// O   O   O - O - O - O - - -
//         - - - - - - - - - -
// 4   O   O - O   O - 3 - - -
//         - - - - - - - - - -
//         - - - - - - - - - -
//         - - - - - - - - - -
//         - - - - - - - - - -
//         - - - - - - - - - -
//
// Scene post-scale, post-offset:
// The X and Y dimensions of the view are now effectively scaled up to 10x10
// (compared to the 9x9 of the screen), with origin at screen space origin.
//   X ->
// Y 1A- O - D - O - O - 2
// | - - - - - - - - - -
// V O - O - O - O - O - O
//   - - - - - - - - - -
//   U - O - M - O - O - O
//   - - - - - - - - - -
//   O - O - O - O - O - O
//   - - - - - - - - - -
//   O - O - O - O - O - O
//   - - - - - - - - - -
//   4   O   O   O   O   3
//
// A - Add event
// D - Down event
// M - Move event
// U - Up event
TEST_F(GfxLegacyCoordinateTransformTest, ClipSpaceTransformed) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  {
    scenic::Session* const session = root_session_->session.get();
    scenic::Scene& scene = root_session_->scene;

    scenic::ViewHolder view_holder(session, std::move(view_holder_token), "view_holder");

    view_holder.SetViewProperties(k5x5x1);
    scene.AddChild(view_holder);

    // Set the clip space transform on the camera.
    // The transform scales everything by 2 around the center of the screen (4.5, 4.5) and then
    // applies offsets in Vulkan normalized device coordinates to bring the origin back
    // to where it was originally. (Parameters are in Vulkan Normalized Device Coordinates.)
    root_session_->camera.SetClipSpaceTransform(/*x offset*/ 1, /*y offset*/ 1, /*scale*/ 2);

    BlockingPresent(*session);
  }

  // Client vends a View to the global scene.
  std::unique_ptr<scenic::Session> child1_session =
      CreateChildView(std::move(view_token), "child1_view");
  std::vector<InputEvent> child1_events;
  child1_session->set_event_handler(
      [&child1_events](std::vector<fuchsia::ui::scenic::Event> events) {
        for (auto& event : events) {
          if (event.is_input() && !event.input().is_focus()) {
            child1_events.emplace_back(std::move(event.input()));
          }
        }
      });

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation. Injected events are in the screen coordinate space.
  {
    scenic::Session* session = root_session_->session.get();

    PointerCommandGenerator pointer(root_session_->compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Add(0.5, 0.5));
    session->Enqueue(pointer.Down(4.5, 0.5));
    session->Enqueue(pointer.Move(4.5, 4.5));
    session->Enqueue(pointer.Up(0.5, 4.5));
  }
  RunLoopUntil([&child1_events] { return child1_events.size() == 4u; });  // Succeeds or times out.

  EXPECT_EQ(child1_events.size(), 4u);
  EXPECT_TRUE(child1_events[0].is_pointer());
  EXPECT_TRUE(PointerMatches(child1_events[0].pointer(), 1u, PointerEventPhase::ADD, 0.25, 0.25));
  EXPECT_TRUE(child1_events[1].is_pointer());
  EXPECT_TRUE(PointerMatches(child1_events[1].pointer(), 1u, PointerEventPhase::DOWN, 2.25, 0.25));
  EXPECT_TRUE(child1_events[2].is_pointer());
  EXPECT_TRUE(PointerMatches(child1_events[2].pointer(), 1u, PointerEventPhase::MOVE, 2.25, 2.25));
  EXPECT_TRUE(child1_events[3].is_pointer());
  EXPECT_TRUE(PointerMatches(child1_events[3].pointer(), 1u, PointerEventPhase::UP, 0.25, 2.25));
}

// In this test we set up a view, apply a ClipSpaceTransform scale to the camera as well as a
// translation on the view holder, and confirm that the delivered coordinates are correctly
// transformed.
//
// Below are ASCII diagrams showing the scene setup.
// Each character is a point on a surface, the top left point representing (0,0)
// and the bottom right (5,5).
// Note that the notated X,Y coordinate system is the screen coordinate system. The view's
// coordinate system has its origin at corner '1'.
//
// Scene pre-transformation (1,2,3,4 denote the corners of the view):
// Note that the view's coordinate system is the same as the screen coordinate system.
//   X ->
// Y 1 O O O O 2 - - - -
// | O O O O O O - - - -
// v O O O O O O - - - -
//   O O O O O O - - - -
//   O O O O O O - - - -
//   4 O O O O 3 - - - -
//   - - - - - - - - - -
//   - - - - - - - - - -
//   - - - - - - - - - -
//   - - - - - - - - - -
//
// Scene after post-clip space transform, pre-translation:
// 1   O   O   O   O   2
//
// O   O   O   O   O   O
//
// O   O   O   O   O   O
//           - - - - - - - - - -
// O   O   O - O - O - O - - - -
//           - - - - - - - - - -
// O   O   O - O - O - O - - - -
//           - - - - - - - - - -
// 4   O   O - O - O - 3 - - - -
//           - - - - - - - - - -
//           - - - - - - - - - -
//           - - - - - - - - - -
//           - - - - - - - - - -
//
// Scene after post-clip space transform, post-translation:
// Size of view is effectively 10x10, translated by (1,1).
// 1   O   O   O   O   2
//
// O   O   O   O   O   O
//       A - - - D - - - - -
// O   O - O - O - O - O - -
//       - - - - - - - - - -
// O   O - O - O - O - O - -
//       U - - - M - - - - -
// O   O - O - O - O - O - -
//       - - - - - - - - - -
// 4   O - O - O - O - 3 - -
//       - - - - - - - - - -
//       - - - - - - - - - -
//
// A - Add event
// D - Down event
// M - Move event
// U - Up event
TEST_F(GfxLegacyCoordinateTransformTest, ClipSpaceAndNodeTransformed) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  {
    scenic::Session* const session = root_session_->session.get();
    scenic::Scene& scene = root_session_->scene;

    scenic::ViewHolder view_holder(session, std::move(view_holder_token), "view_holder");

    view_holder.SetViewProperties(k5x5x1);
    scene.AddChild(view_holder);

    // Set the clip space transform to zoom in on the center of the screen.
    root_session_->camera.SetClipSpaceTransform(0, 0, /*scale*/ 2);
    // Translate view holder.
    view_holder.SetTranslation(1, 1, 0);

    BlockingPresent(*session);
  }

  // Client vends a View to the global scene.
  std::unique_ptr<scenic::Session> child1_session =
      CreateChildView(std::move(view_token), "child1_view");
  std::vector<InputEvent> child1_events;
  child1_session->set_event_handler(
      [&child1_events](std::vector<fuchsia::ui::scenic::Event> events) {
        for (auto& event : events) {
          if (event.is_input() && !event.input().is_focus()) {
            child1_events.emplace_back(std::move(event.input()));
          }
        }
      });

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation. Injected events are in the screen coordinate space.
  {
    scenic::Session* session = root_session_->session.get();

    PointerCommandGenerator pointer(root_session_->compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Add(0.5, 0.5));
    session->Enqueue(pointer.Down(4.5, 0.5));
    session->Enqueue(pointer.Move(4.5, 4.5));
    session->Enqueue(pointer.Up(0.5, 4.5));
  }
  RunLoopUntil([&child1_events] { return child1_events.size() == 4u; });  // Succeeds or times out.

  EXPECT_EQ(child1_events.size(), 4u);
  EXPECT_TRUE(child1_events[0].is_pointer());
  EXPECT_TRUE(
      PointerMatches(child1_events[0].pointer(), 1u, PointerEventPhase::ADD, 2.5 - 1, 2.5 - 1));
  EXPECT_TRUE(child1_events[1].is_pointer());
  EXPECT_TRUE(
      PointerMatches(child1_events[1].pointer(), 1u, PointerEventPhase::DOWN, 4.5 - 1, 2.5 - 1));
  EXPECT_TRUE(child1_events[2].is_pointer());
  EXPECT_TRUE(
      PointerMatches(child1_events[2].pointer(), 1u, PointerEventPhase::MOVE, 4.5 - 1, 4.5 - 1));
  EXPECT_TRUE(child1_events[3].is_pointer());
  EXPECT_TRUE(
      PointerMatches(child1_events[3].pointer(), 1u, PointerEventPhase::UP, 2.5 - 1, 4.5 - 1));
}

}  // namespace integration_tests
