// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
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

// These tests exercise the "context View Space to target View Space" coordinate transform logic,
// applied to pointer events sent to sessions using the input injection API.
// Setup:
// - Injection done in context View Space, with fuchsia.ui.pointerinjector
// - Target(s) specified by View (using view ref koids)
// - Dispatch done in fuchsia.ui.scenic.SessionListener (legacy)

namespace integration_tests {

using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;
using fuchsia::ui::views::ViewRef;
static constexpr fuchsia::ui::gfx::ViewProperties k5x5x1 = {.bounding_box = {.max = {5, 5, 1}}};

const std::map<std::string, std::string> LocalServices() {
  return {{"fuchsia.ui.composition.Allocator",
           "fuchsia-pkg://fuchsia.com/gfx_integration_tests#meta/scenic.cmx"},
          {"fuchsia.ui.scenic.Scenic",
           "fuchsia-pkg://fuchsia.com/gfx_integration_tests#meta/scenic.cmx"},
          {"fuchsia.ui.pointerinjector.Registry",
           "fuchsia-pkg://fuchsia.com/gfx_integration_tests#meta/scenic.cmx"},
          {"fuchsia.hardware.display.Provider",
           "fuchsia-pkg://fuchsia.com/fake-hardware-display-controller-provider#meta/hdcp.cmx"}};
}

// Allow these global services.
const std::vector<std::string> GlobalServices() {
  return {"fuchsia.vulkan.loader.Loader", "fuchsia.sysmem.Allocator"};
}

using fuchsia::ui::views::ViewRef;

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

class GfxLegacyCoordinateTransformTest2 : public sys::testing::TestWithEnvironment {
  // clang-format off
  static constexpr std::array<float, 9> kIdentityMatrix = {
    1, 0, 0, // column one
    0, 1, 0, // column two
    0, 0, 1, // column three
  };
  // clang-format on

 protected:
  fuchsia::ui::scenic::Scenic* scenic() { return scenic_.get(); }

  void SetUp() override {
    TestWithEnvironment::SetUp();
    environment_ = CreateNewEnclosingEnvironment(
        "gfx_legacy_coordinate_transform_test2_environment", CreateServices());
    environment_->ConnectToService(scenic_.NewRequest());
    scenic_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
    });
    environment_->ConnectToService(registry_.NewRequest());
    registry_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to pointerinjector Registry: " << zx_status_get_string(status);
    });

    // Set up root view.
    root_session_ = std::make_unique<RootSession>(scenic());
    root_session_->session->set_error_handler([](auto) { FAIL() << "Root session terminated."; });
    BlockingPresent(*root_session_->session);
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

  void BlockingPresent(scenic::Session& session) {
    bool presented = false;
    session.set_on_frame_presented_handler([&presented](auto) { presented = true; });
    session.Present2(0, 0, [](auto) {});
    RunLoopUntil([&presented] { return presented; });
    session.set_on_frame_presented_handler([](auto) {});
  }

  std::unique_ptr<scenic::Session> CreateChildView(fuchsia::ui::views::ViewToken view_token,
                                                   fuchsia::ui::views::ViewRefControl control_ref,
                                                   fuchsia::ui::views::ViewRef view_ref,
                                                   std::string debug_name) {
    auto session = CreateSession(scenic());
    scenic::View view(session.get(), std::move(view_token), std::move(control_ref),
                      std::move(view_ref), debug_name);
    scenic::ShapeNode shape(session.get());
    scenic::Rectangle rec(session.get(), 5, 5);
    shape.SetTranslation(2.5f, 2.5f, 0);  // Center the shape within the View.
    view.AddChild(shape);
    shape.SetShape(rec);
    BlockingPresent(*session);

    return session;
  }

  void Inject(float x, float y, fuchsia::ui::pointerinjector::EventPhase phase) {
    FX_CHECK(injector_);
    fuchsia::ui::pointerinjector::Event event;
    event.set_timestamp(0);
    {
      fuchsia::ui::pointerinjector::PointerSample pointer_sample;
      pointer_sample.set_pointer_id(1);
      pointer_sample.set_phase(phase);
      pointer_sample.set_position_in_viewport({x, y});
      fuchsia::ui::pointerinjector::Data data;
      data.set_pointer_sample(std::move(pointer_sample));
      event.set_data(std::move(data));
    }
    std::vector<fuchsia::ui::pointerinjector::Event> events;
    events.emplace_back(std::move(event));
    injector_->Inject(std::move(events), [] {});
  }

  void RegisterInjector(fuchsia::ui::views::ViewRef context_view_ref,
                        fuchsia::ui::views::ViewRef target_view_ref,
                        fuchsia::ui::pointerinjector::DispatchPolicy dispatch_policy =
                            fuchsia::ui::pointerinjector::DispatchPolicy::EXCLUSIVE_TARGET,
                        std::array<float, 9> viewport_to_context_transform = kIdentityMatrix) {
    fuchsia::ui::pointerinjector::Config config;
    config.set_device_id(1);
    config.set_device_type(fuchsia::ui::pointerinjector::DeviceType::TOUCH);
    config.set_dispatch_policy(dispatch_policy);
    {
      {
        fuchsia::ui::pointerinjector::Context context;
        context.set_view(std::move(context_view_ref));
        config.set_context(std::move(context));
      }
      {
        fuchsia::ui::pointerinjector::Target target;
        target.set_view(std::move(target_view_ref));
        config.set_target(std::move(target));
      }
      {
        fuchsia::ui::pointerinjector::Viewport viewport;
        viewport.set_extents(FullScreenExtents());
        viewport.set_viewport_to_context_transform(viewport_to_context_transform);
        config.set_viewport(std::move(viewport));
      }
    }

    bool error_callback_fired = false;
    injector_.set_error_handler([&error_callback_fired](zx_status_t) {
      FX_LOGS(ERROR) << "Channel closed.";
      error_callback_fired = true;
    });
    bool register_callback_fired = false;
    registry_->Register(std::move(config), injector_.NewRequest(),
                        [&register_callback_fired] { register_callback_fired = true; });
    RunLoopUntil([&register_callback_fired] { return register_callback_fired; });
    ASSERT_FALSE(error_callback_fired);
  }

  std::array<std::array<float, 2>, 2> FullScreenExtents() const { return {{{0, 0}, {9, 9}}}; }

  std::unique_ptr<RootSession> root_session_;

 private:
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
  fuchsia::ui::scenic::ScenicPtr scenic_;
  fuchsia::ui::pointerinjector::RegistryPtr registry_;
  fuchsia::ui::pointerinjector::DevicePtr injector_;
};

// In this test we set up the context and the target. We apply a scale, rotation and translation
// transform to both of their view holder nodes, and then inject pointer events to confirm that
// the coordinates received by the listener are correctly transformed.
// Only the transformation of the target, relative to the context, should have any effect on
// the output.
// The viewport-to-context transform here is the identity.
//
// Below are ASCII diagrams showing the transformation *difference* between target and context.
// Note that the dashes represent the context view and notated X,Y coordinate system is the
// context's coordinate system. The target view's coordinate system has its origin at corner '1'.
//
// Scene pre-transformation
// 1,2,3,4 denote the corners of the target view:
//   X ->
// Y 1 O O O O 2
// | O O O O O O
// v O O O O O O
//   O O O O O O
//   O O O O O O
//   4 O O O O 3
//
// After scale:
//   X ->
// Y 1 - O - O - O   O   2
// | - - - - - - -
// V - - - - - - -
//   O - O - O - O   O   O
//   - - - - - - -
//   - - - - - - -
//   O   O   O   O   O   O
//
//
//   O   O   O   O   O   O
//
//
//   O   O   O   O   O   O
//
//
//   4   O   O   O   O   3
//
// After rotation:
//   X ->
// Y 4      O      O      O      O      1 - - - - - -
// |                                      - - - - - -
// V O      O      O      O      O      O - - - - - -
//                                        - - - - - -
//   O      O      O      O      O      O - - - - - -
//                                        - - - - - -
//   O      O      O      O      O      O
//
//   O      O      O      O      O      O
//
//   3      O      O      O      O      2
//
// After translation:
//   X ->
// Y 4      O      O      O      O    D 1 - - - C1
// |                                  - - - - - -
// V O      O      O      O      O    - O - - - -
//                                    - - - - - -
//   O      O      O      O      O    - O - - - -
//                                    U - - - - C2
//   O      O      O      O      O      O
//
//   O      O      O      O      O      O
//
//   3      O      O      O      O      2
//
TEST_F(GfxLegacyCoordinateTransformTest2, InjectedInput_ShouldBeCorrectlyTransformed) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up a scene with two ViewHolders, one a child of the other.
  scenic::ViewHolder holder_1(root_session_->session.get(), std::move(vh1), "holder_1");
  {
    root_session_->scene.AddChild(holder_1);
    holder_1.SetViewProperties(k5x5x1);
    // Scale, rotate and translate the context to verify that it has no effect on the outcome.
    holder_1.SetScale(2, 3, 1);
    const auto rotation_quaternion = glm::angleAxis(glm::pi<float>() / 2.f, glm::vec3(0, 0, 1));
    holder_1.SetRotation(rotation_quaternion.x, rotation_quaternion.y, rotation_quaternion.z,
                         rotation_quaternion.w);
    holder_1.SetTranslation(1, 0, 0);
    BlockingPresent(*root_session_->session);
  }

  std::unique_ptr<scenic::Session> child1_session = CreateSession(scenic());
  std::vector<InputEvent> child1_events;
  child1_session->set_event_handler(
      [&child1_events](std::vector<fuchsia::ui::scenic::Event> events) {
        for (auto& event : events) {
          if (event.is_input() && !event.input().is_focus()) {
            child1_events.emplace_back(std::move(event.input()));
          }
        }
      });
  auto [child1_control_ref, child1_view_ref] = scenic::ViewRefPair::New();
  {
    scenic::View view(child1_session.get(), std::move(v1), std::move(child1_control_ref),
                      fidl::Clone(child1_view_ref), "child1_view");
    scenic::ViewHolder holder_2(child1_session.get(), std::move(vh2), "holder_2");
    view.AddChild(holder_2);
    holder_2.SetViewProperties(k5x5x1);
    // Scale, rotate and translate target.
    // Scale X by 2 and Y by 3.
    holder_2.SetScale(2, 3, 1);
    // Rotate 90 degrees counter clockwise around Z-axis (Z-axis points into screen, so appears as
    // clockwise rotation).
    const auto rotation_quaternion = glm::angleAxis(glm::pi<float>() / 2.f, glm::vec3(0, 0, 1));
    holder_2.SetRotation(rotation_quaternion.x, rotation_quaternion.y, rotation_quaternion.z,
                         rotation_quaternion.w);
    // Translate by 1 in the X direction.
    holder_2.SetTranslation(1, 0, 0);
    BlockingPresent(*child1_session);
  }

  auto [child2_control_ref, child2_view_ref] = scenic::ViewRefPair::New();
  auto child2_session = CreateChildView(std::move(v2), std::move(child2_control_ref),
                                        fidl::Clone(child2_view_ref), "child2_view");
  std::vector<InputEvent> child2_events;
  child2_session->set_event_handler(
      [&child2_events](std::vector<fuchsia::ui::scenic::Event> events) {
        for (auto& event : events) {
          if (event.is_input() && !event.input().is_focus()) {
            child2_events.emplace_back(std::move(event.input()));
          }
        }
      });

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation.
  RegisterInjector(std::move(child1_view_ref), std::move(child2_view_ref));
  Inject(0, 0, fuchsia::ui::pointerinjector::EventPhase::ADD);
  Inject(5, 0, fuchsia::ui::pointerinjector::EventPhase::CHANGE);
  Inject(5, 5, fuchsia::ui::pointerinjector::EventPhase::CHANGE);
  Inject(0, 5, fuchsia::ui::pointerinjector::EventPhase::REMOVE);
  RunLoopUntil([&child2_events] { return child2_events.size() == 6u; });  // Succeeds or times out.

  // Context should receive no events.
  EXPECT_TRUE(child1_events.empty());

  // Targets gets properly transformed input coordinates.
  EXPECT_TRUE(
      PointerMatches(child2_events[0].pointer(), 1u, PointerEventPhase::ADD, 0.f / 2.f, 1.f / 3.f));
  EXPECT_TRUE(PointerMatches(child2_events[1].pointer(), 1u, PointerEventPhase::DOWN, 0.f / 2.f,
                             1.f / 3.f));
  // TODO(fxbug.dev/81710): Coordinates clamped to their owning view.
  EXPECT_TRUE(
      PointerMatches(child2_events[2].pointer(), 1u, PointerEventPhase::MOVE, 0.f / 2.f, 0.f));
  EXPECT_TRUE(
      PointerMatches(child2_events[3].pointer(), 1u, PointerEventPhase::MOVE, 5.f / 2.f, 0.f));
  EXPECT_TRUE(
      PointerMatches(child2_events[4].pointer(), 1u, PointerEventPhase::UP, 5.f / 2.f, 1.f / 3.f));
  EXPECT_TRUE(PointerMatches(child2_events[5].pointer(), 1u, PointerEventPhase::REMOVE, 5.f / 2.f,
                             1.f / 3.f));
}

// In this test the context and the target have identical coordinate systems, but the viewport
// no longer matches the context's coordinate system.
//
// Below is an ASCII diagram showing the resulting setup.
// O represents the views, - the viewport.
//   X ->
// Y O   O   O   O   O   O
// |
// V   D - - - - C1- - - -
//   O - O - O - O - O - O
//     - - - - - - - - - -
//     - - - - - - - - - -
//   O - O - O - O - O - O
//     U - - - - C2- - - -
//     - - - - - - - - - -
//   O - O - O - O - O - O
//     - - - - - - - - - -
//     - - - - - - - - - -
//   O   O   O   O   O   O
//
//
//   O   O   O   O   O   O
//
TEST_F(GfxLegacyCoordinateTransformTest2, InjectedInput_ShouldBeCorrectlyViewportTransformed) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up a scene with two ViewHolders, one a child of the other.
  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  {
    scenic::View view(root_session_->session.get(), std::move(v1), std::move(root_control_ref),
                      fidl::Clone(root_view_ref), "child1_view");
    scenic::ViewHolder holder_1(root_session_->session.get(), std::move(vh1), "holder_1");
    holder_1.SetViewProperties(k5x5x1);
    root_session_->scene.AddChild(holder_1);
    scenic::ViewHolder holder_2(root_session_->session.get(), std::move(vh2), "holder_2");
    holder_2.SetViewProperties(k5x5x1);
    view.AddChild(holder_2);
    BlockingPresent(*root_session_->session);
  }

  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  auto child_session = CreateChildView(std::move(v2), std::move(child_control_ref),
                                       fidl::Clone(child_view_ref), "child_view");
  std::vector<InputEvent> child_events;
  child_session->set_event_handler([&child_events](std::vector<fuchsia::ui::scenic::Event> events) {
    for (auto& event : events) {
      if (event.is_input() && !event.input().is_focus()) {
        child_events.emplace_back(std::move(event.input()));
      }
    }
  });

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation.

  // Transform to scale the viewport by 1/2 in the x-direction, 1/3 in the y-direction,
  // and then translate by (1, 2).
  // clang-format off
  static constexpr std::array<float, 9> kViewportToContextTransform = {
    1.f/2.f,  0,  0, // first column
    0,  1.f/3.f,  0, // second column
    1,      2,  1, // third column
  };
  // clang-format on

  RegisterInjector(std::move(root_view_ref), std::move(child_view_ref),
                   fuchsia::ui::pointerinjector::DispatchPolicy::EXCLUSIVE_TARGET,
                   kViewportToContextTransform);
  Inject(0, 0, fuchsia::ui::pointerinjector::EventPhase::ADD);
  Inject(5, 0, fuchsia::ui::pointerinjector::EventPhase::CHANGE);
  Inject(5, 5, fuchsia::ui::pointerinjector::EventPhase::CHANGE);
  Inject(0, 5, fuchsia::ui::pointerinjector::EventPhase::REMOVE);
  RunLoopUntil([&child_events] { return child_events.size() == 6u; });  // Succeeds or times out.

  // Targets gets properly transformed input coordinates.
  EXPECT_TRUE(PointerMatches(child_events[0].pointer(), 1u, PointerEventPhase::ADD, 0.f / 2.f + 1,
                             0.f / 3.f + 2));
  EXPECT_TRUE(PointerMatches(child_events[1].pointer(), 1u, PointerEventPhase::DOWN, 0.f / 2.f + 1,
                             0.f / 3.f + 2));
  EXPECT_TRUE(PointerMatches(child_events[2].pointer(), 1u, PointerEventPhase::MOVE, 5.f / 2.f + 1,
                             0.f / 3.f + 2));
  EXPECT_TRUE(PointerMatches(child_events[3].pointer(), 1u, PointerEventPhase::MOVE, 5.f / 2.f + 1,
                             5.f / 3.f + 2));
  EXPECT_TRUE(PointerMatches(child_events[4].pointer(), 1u, PointerEventPhase::UP, 0.f / 2.f + 1,
                             5.f / 3.f + 2));
  EXPECT_TRUE(PointerMatches(child_events[5].pointer(), 1u, PointerEventPhase::REMOVE,
                             0.f / 2.f + 1, 5.f / 3.f + 2));
}

// In this test the context and the target have identical coordinate systems except for a 90 degree
// rotation. Check that all corners still generate hits. This confirms that small floating point
// errors don't cause misses.
//
// Scene pre-transformation
// 1,2,3,4 denote the corners of the target view:
//   X ->
// Y 1 O O O O 2
// | O O O O O O
// v O O O O O O
//   O O O O O O
//   O O O O O O
//   4 O O O O 3
//
// Post-rotation
//   X ->
// Y 4 O O O O 1
// | O O O O O O
// v O O O O O O
//   O O O O O O
//   O O O O O O
//   3 O O O O 2
TEST_F(GfxLegacyCoordinateTransformTest2, InjectedInput_OnRotatedChild_ShouldHitEdges) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up a scene with two ViewHolders, one a child of the other.
  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  {
    scenic::View view(root_session_->session.get(), std::move(v1), std::move(root_control_ref),
                      fidl::Clone(root_view_ref), "child1_view");
    scenic::ViewHolder holder_1(root_session_->session.get(), std::move(vh1), "holder_1");
    holder_1.SetViewProperties(k5x5x1);
    root_session_->scene.AddChild(holder_1);
    scenic::ViewHolder holder_2(root_session_->session.get(), std::move(vh2), "holder_2");
    holder_2.SetViewProperties(k5x5x1);
    // Rotate 90 degrees counter clockwise around Z-axis (Z-axis points into screen, so appears as
    // clockwise rotation).
    holder_2.SetAnchor(2.5f, 2.5f, 0);
    const auto rotation_quaternion = glm::angleAxis(glm::pi<float>() / 2.f, glm::vec3(0, 0, 1));
    holder_2.SetRotation(rotation_quaternion.x, rotation_quaternion.y, rotation_quaternion.z,
                         rotation_quaternion.w);
    view.AddChild(holder_2);
    BlockingPresent(*root_session_->session);
  }

  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  auto child_session = CreateChildView(std::move(v2), std::move(child_control_ref),
                                       fidl::Clone(child_view_ref), "child_view");
  std::vector<InputEvent> child_events;
  child_session->set_event_handler([&child_events](std::vector<fuchsia::ui::scenic::Event> events) {
    for (auto& event : events) {
      if (event.is_input() && !event.input().is_focus()) {
        child_events.emplace_back(std::move(event.input()));
      }
    }
  });

  // Scene is now set up, send in the input. One interaction for each corner.
  RegisterInjector(std::move(root_view_ref), std::move(child_view_ref),
                   fuchsia::ui::pointerinjector::DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET);
  Inject(0, 0, fuchsia::ui::pointerinjector::EventPhase::ADD);
  Inject(0, 0, fuchsia::ui::pointerinjector::EventPhase::REMOVE);
  Inject(0, 5, fuchsia::ui::pointerinjector::EventPhase::ADD);
  Inject(0, 5, fuchsia::ui::pointerinjector::EventPhase::REMOVE);
  Inject(5, 5, fuchsia::ui::pointerinjector::EventPhase::ADD);
  Inject(5, 5, fuchsia::ui::pointerinjector::EventPhase::REMOVE);
  Inject(5, 0, fuchsia::ui::pointerinjector::EventPhase::ADD);
  Inject(5, 0, fuchsia::ui::pointerinjector::EventPhase::REMOVE);
  RunLoopUntil([&child_events] { return child_events.size() == 16u; });  // Succeeds or times out.

  // Target should receive all events rotated 90 degrees.
  EXPECT_TRUE(PointerMatches(child_events[0].pointer(), 1u, PointerEventPhase::ADD, 0.f, 5.f));
  EXPECT_TRUE(PointerMatches(child_events[1].pointer(), 1u, PointerEventPhase::DOWN, 0.f, 5.f));
  EXPECT_TRUE(PointerMatches(child_events[2].pointer(), 1u, PointerEventPhase::UP, 0.f, 5.f));
  EXPECT_TRUE(PointerMatches(child_events[3].pointer(), 1u, PointerEventPhase::REMOVE, 0.f, 5.f));
  EXPECT_TRUE(PointerMatches(child_events[4].pointer(), 1u, PointerEventPhase::ADD, 5.f, 5.f));
  EXPECT_TRUE(PointerMatches(child_events[5].pointer(), 1u, PointerEventPhase::DOWN, 5.f, 5.f));
  EXPECT_TRUE(PointerMatches(child_events[6].pointer(), 1u, PointerEventPhase::UP, 5.f, 5.f));
  EXPECT_TRUE(PointerMatches(child_events[7].pointer(), 1u, PointerEventPhase::REMOVE, 5.f, 5.f));
  EXPECT_TRUE(PointerMatches(child_events[8].pointer(), 1u, PointerEventPhase::ADD, 5.f, 0.f));
  EXPECT_TRUE(PointerMatches(child_events[9].pointer(), 1u, PointerEventPhase::DOWN, 5.f, 0.f));
  EXPECT_TRUE(PointerMatches(child_events[10].pointer(), 1u, PointerEventPhase::UP, 5.f, 0.f));
  EXPECT_TRUE(PointerMatches(child_events[11].pointer(), 1u, PointerEventPhase::REMOVE, 5.f, 0.f));
  EXPECT_TRUE(PointerMatches(child_events[12].pointer(), 1u, PointerEventPhase::ADD, 0.f, 0.f));
  EXPECT_TRUE(PointerMatches(child_events[13].pointer(), 1u, PointerEventPhase::DOWN, 0.f, 0.f));
  EXPECT_TRUE(PointerMatches(child_events[14].pointer(), 1u, PointerEventPhase::UP, 0.f, 0.f));
  EXPECT_TRUE(PointerMatches(child_events[15].pointer(), 1u, PointerEventPhase::REMOVE, 0.f, 0.f));
}

// In this test we set up the context and the target. We apply clip space transform to the camera
// and then inject pointer events to confirm that the coordinates received by the listener are
// not impacted by the clip space transform.
TEST_F(GfxLegacyCoordinateTransformTest2, ClipSpaceTransformedScene_ShouldHaveNoImpactOnOutput) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set the clip space transform on the camera.
  // Camera zooms in by 3x, and the camera is moved to (24,54) in the scene's coordinate space.
  root_session_->camera.SetClipSpaceTransform(/*x offset*/ 24, /*y offset*/ 54, /*scale*/ 3);

  // Set up a scene with two ViewHolders, one a child of the other.
  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  {
    scenic::View view(root_session_->session.get(), std::move(v1), std::move(root_control_ref),
                      fidl::Clone(root_view_ref), "child1_view");
    scenic::ViewHolder holder_1(root_session_->session.get(), std::move(vh1), "holder_1");
    holder_1.SetViewProperties(k5x5x1);
    root_session_->scene.AddChild(holder_1);
    scenic::ViewHolder holder_2(root_session_->session.get(), std::move(vh2), "holder_2");
    holder_2.SetViewProperties(k5x5x1);
    view.AddChild(holder_2);
    BlockingPresent(*root_session_->session);
  }

  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  auto child_session = CreateChildView(std::move(v2), std::move(child_control_ref),
                                       fidl::Clone(child_view_ref), "child_view");
  std::vector<InputEvent> child_events;
  child_session->set_event_handler([&child_events](std::vector<fuchsia::ui::scenic::Event> events) {
    for (auto& event : events) {
      if (event.is_input() && !event.input().is_focus()) {
        child_events.emplace_back(std::move(event.input()));
      }
    }
  });

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation.
  RegisterInjector(std::move(root_view_ref), std::move(child_view_ref));
  Inject(0, 0, fuchsia::ui::pointerinjector::EventPhase::ADD);
  Inject(5, 0, fuchsia::ui::pointerinjector::EventPhase::CHANGE);
  Inject(5, 5, fuchsia::ui::pointerinjector::EventPhase::CHANGE);
  Inject(0, 5, fuchsia::ui::pointerinjector::EventPhase::REMOVE);
  RunLoopUntil([&child_events] { return child_events.size() == 6u; });  // Succeeds or times out.

  // Target should receive identical events to injected, since their coordinate spaces are the same.
  EXPECT_TRUE(PointerMatches(child_events[0].pointer(), 1u, PointerEventPhase::ADD, 0.f, 0.f));
  EXPECT_TRUE(PointerMatches(child_events[1].pointer(), 1u, PointerEventPhase::DOWN, 0.f, 0.f));
  EXPECT_TRUE(PointerMatches(child_events[2].pointer(), 1u, PointerEventPhase::MOVE, 5.f, 0.f));
  EXPECT_TRUE(PointerMatches(child_events[3].pointer(), 1u, PointerEventPhase::MOVE, 5.f, 5.f));
  EXPECT_TRUE(PointerMatches(child_events[4].pointer(), 1u, PointerEventPhase::UP, 0.f, 5.f));
  EXPECT_TRUE(PointerMatches(child_events[5].pointer(), 1u, PointerEventPhase::REMOVE, 0.f, 5.f));
}

// Basic scene (no transformations) where the Viewport is smaller than the Views.
// We then inject two streams: The first has an ADD outside the Viewport, which counts as a miss and
// should not be seen by anyone. The second stream has the ADD inside the Viewport and subsequent
// events outside, and this full stream should be seen by the target.
TEST_F(GfxLegacyCoordinateTransformTest2, InjectionOutsideViewport_ShouldLimitOnADD) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up a scene with two ViewHolders, one a child of the other. Make the Views bigger than the\
  // Viewport.
  static constexpr fuchsia::ui::gfx::ViewProperties k100x100x1 = {
      .bounding_box = {.max = {100, 100, 1}}};
  // Set up a scene with two ViewHolders, one a child of the other.
  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  {
    scenic::View view(root_session_->session.get(), std::move(v1), std::move(root_control_ref),
                      fidl::Clone(root_view_ref), "child1_view");
    scenic::ViewHolder holder_1(root_session_->session.get(), std::move(vh1), "holder_1");
    holder_1.SetViewProperties(k100x100x1);
    root_session_->scene.AddChild(holder_1);
    scenic::ViewHolder holder_2(root_session_->session.get(), std::move(vh2), "holder_2");
    holder_2.SetViewProperties(k100x100x1);
    view.AddChild(holder_2);
    BlockingPresent(*root_session_->session);
  }

  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  auto child_session = CreateChildView(std::move(v2), std::move(child_control_ref),
                                       fidl::Clone(child_view_ref), "child_view");
  std::vector<InputEvent> child_events;
  child_session->set_event_handler([&child_events](std::vector<fuchsia::ui::scenic::Event> events) {
    for (auto& event : events) {
      if (event.is_input() && !event.input().is_focus()) {
        child_events.emplace_back(std::move(event.input()));
      }
    }
  });

  // Scene is now set up, send in the input. The initial input is outside the viewport and
  // the stream should therefore not be seen by anyone.
  RegisterInjector(std::move(root_view_ref), std::move(child_view_ref),
                   fuchsia::ui::pointerinjector::DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET);
  Inject(10, 10, fuchsia::ui::pointerinjector::EventPhase::ADD);  // Outside viewport.
  // Rest inside viewport, but should not be delivered.
  Inject(5, 0, fuchsia::ui::pointerinjector::EventPhase::CHANGE);
  Inject(5, 5, fuchsia::ui::pointerinjector::EventPhase::CHANGE);
  Inject(0, 5, fuchsia::ui::pointerinjector::EventPhase::REMOVE);
  RunLoopWithTimeout(zx::sec(1));
  EXPECT_TRUE(child_events.empty());

  // Send in input starting in the viewport and moving outside.
  Inject(1, 1, fuchsia::ui::pointerinjector::EventPhase::ADD);  // Inside viewport.
  // Rest outside viewport, but should still be delivered.
  Inject(50, 0, fuchsia::ui::pointerinjector::EventPhase::CHANGE);
  Inject(50, 50, fuchsia::ui::pointerinjector::EventPhase::CHANGE);
  Inject(0, 50, fuchsia::ui::pointerinjector::EventPhase::REMOVE);
  RunLoopUntil([&child_events] { return child_events.size() == 6u; });  // Succeeds or times out.

  EXPECT_TRUE(PointerMatches(child_events[0].pointer(), 1u, PointerEventPhase::ADD, 1.f, 1.f));
  EXPECT_TRUE(PointerMatches(child_events[1].pointer(), 1u, PointerEventPhase::DOWN, 1.f, 1.f));
  EXPECT_TRUE(PointerMatches(child_events[2].pointer(), 1u, PointerEventPhase::MOVE, 50.f, 0.f));
  EXPECT_TRUE(PointerMatches(child_events[3].pointer(), 1u, PointerEventPhase::MOVE, 50.f, 50.f));
  EXPECT_TRUE(PointerMatches(child_events[4].pointer(), 1u, PointerEventPhase::UP, 0.f, 50.f));
  EXPECT_TRUE(PointerMatches(child_events[5].pointer(), 1u, PointerEventPhase::REMOVE, 0.f, 50.f));
}

}  // namespace integration_tests
