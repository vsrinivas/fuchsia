// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/pointer/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/lib/glm_workaround/glm_workaround.h"
#include "src/ui/scenic/integration_tests/utils.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

// These tests exercise the integration between GFX and the InputSystem, including the View-to-View
// transform logic between the injection point and the receiver.
// Setup:
// - Injection done in context View Space, with fuchsia.ui.pointerinjector
// - Target(s) specified by View (using view ref koids)
// - Dispatch done to fuchsia.ui.pointer.TouchSource in receiver View Space.

// Macro for calling EXPECT on fuchsia::ui::pointer::PointerSample.
// Is a macro to ensure we get the correct line number for the error.
#define EXPECT_EQ_POINTER(pointer_sample, viewport_to_view_transform, expected_phase, expected_x, \
                          expected_y)                                                             \
  {                                                                                               \
    constexpr float kEpsilon = std::numeric_limits<float>::epsilon() * 1000;                      \
    EXPECT_EQ(pointer_sample.phase(), expected_phase);                                            \
    const glm::mat3 transform_matrix = ArrayToMat3(viewport_to_view_transform);                   \
    const std::array<float, 2> transformed_pointer =                                              \
        TransformPointerCoords(pointer_sample.position_in_viewport(), transform_matrix);          \
    EXPECT_NEAR(transformed_pointer[0], expected_x, kEpsilon);                                    \
    EXPECT_NEAR(transformed_pointer[1], expected_y, kEpsilon);                                    \
  }

namespace integration_tests {

using fuchsia::ui::pointer::TouchInteractionStatus;
using fuchsia::ui::pointerinjector::DispatchPolicy;
using fupi_EventPhase = fuchsia::ui::pointerinjector::EventPhase;
using fuchsia::ui::pointer::EventPhase;
using fuchsia::ui::pointer::TouchEvent;
using fuchsia::ui::pointer::TouchResponse;
using fuchsia::ui::pointer::TouchResponseType;
using fuchsia::ui::views::ViewRef;

namespace {

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

glm::mat3 ArrayToMat3(std::array<float, 9> array) {
  // clang-format off
  return glm::mat3(array[0], array[1], array[2],  // first column
                   array[3], array[4], array[5],  // second column
                   array[6], array[7], array[8]); // third column
  // clang-format on
}

std::array<float, 2> TransformPointerCoords(std::array<float, 2> pointer,
                                            const glm::mat3& transform) {
  const glm::vec3 homogenous_pointer{pointer[0], pointer[1], 1};
  const glm::vec3 transformed_pointer = transform * homogenous_pointer;
  const glm::vec3 homogenized = transformed_pointer / transformed_pointer.z;
  return {homogenized.x, homogenized.y};
}

struct SessionWithTouchSource {
  std::unique_ptr<scenic::Session> session;
  fuchsia::ui::pointer::TouchSourcePtr touch_source_ptr;
};

SessionWithTouchSource CreateSessionWithTouchSource(fuchsia::ui::scenic::Scenic* scenic) {
  SessionWithTouchSource session_with_touch_source;

  fuchsia::ui::scenic::SessionEndpoints endpoints;
  fuchsia::ui::scenic::SessionPtr session_ptr;
  fuchsia::ui::scenic::SessionListenerHandle listener_handle;
  auto listener_request = listener_handle.NewRequest();
  endpoints.set_session(session_ptr.NewRequest());
  endpoints.set_session_listener(std::move(listener_handle));
  endpoints.set_touch_source(session_with_touch_source.touch_source_ptr.NewRequest());
  scenic->CreateSessionT(std::move(endpoints), [] {});

  session_with_touch_source.session =
      std::make_unique<scenic::Session>(std::move(session_ptr), std::move(listener_request));
  return session_with_touch_source;
}

// Sets up the root of a scene.
// Present() must be called separately by the creator, since this does not have access to the
// looper.
struct RootSession {
  RootSession(fuchsia::ui::scenic::Scenic* scenic)
      : session_with_touch_source(CreateSessionWithTouchSource(scenic)),
        session(session_with_touch_source.session.get()),
        compositor(session),
        layer_stack(session),
        layer(session),
        renderer(session),
        scene(session),
        camera(scene) {
    compositor.SetLayerStack(layer_stack);
    layer_stack.AddLayer(layer);
    layer.SetSize(/*width*/ 9, /*height*/ 9);  // 9x9 "display".
    layer.SetRenderer(renderer);
    renderer.SetCamera(camera);
  }

  SessionWithTouchSource session_with_touch_source;
  scenic::Session* session = nullptr;
  scenic::DisplayCompositor compositor;
  scenic::LayerStack layer_stack;
  scenic::Layer layer;
  scenic::Renderer renderer;
  scenic::Scene scene;
  scenic::Camera camera;

  std::unique_ptr<scenic::ViewHolder> view_holder;
};

}  // namespace

class GfxTouchIntegrationTest : public gtest::TestWithEnvironmentFixture {
 protected:
  static constexpr fuchsia::ui::gfx::ViewProperties k5x5x1 = {.bounding_box = {.max = {5, 5, 1}}};
  static constexpr uint32_t kDeviceId = 1111;
  static constexpr uint32_t kPointerId = 2222;
  // clang-format off
  static constexpr std::array<float, 9> kIdentityMatrix = {
    1, 0, 0, // column one
    0, 1, 0, // column two
    0, 0, 1, // column three
  };
  // clang-format on

  fuchsia::ui::scenic::Scenic* scenic() { return scenic_.get(); }

  void SetUp() override {
    TestWithEnvironmentFixture::SetUp();
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
  // shadows but calls |TestWithEnvironmentFixture::CreateServices()|.
  std::unique_ptr<sys::testing::EnvironmentServices> CreateServices() {
    auto services = TestWithEnvironmentFixture::CreateServices();
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

  SessionWithTouchSource CreateChildView(fuchsia::ui::views::ViewToken view_token,
                                         fuchsia::ui::views::ViewRefControl control_ref,
                                         fuchsia::ui::views::ViewRef view_ref,
                                         std::string debug_name) {
    auto session_with_touch_source = CreateSessionWithTouchSource(scenic());
    scenic::Session* session = session_with_touch_source.session.get();
    scenic::View view(session, std::move(view_token), std::move(control_ref), std::move(view_ref),
                      debug_name);
    scenic::ShapeNode shape(session);
    scenic::Rectangle rec(session, 5, 5);
    shape.SetTranslation(2.5f, 2.5f, 0);  // Center the shape within the View.
    view.AddChild(shape);
    shape.SetShape(rec);
    BlockingPresent(*session);

    return session_with_touch_source;
  }

  void Inject(float x, float y, fupi_EventPhase phase) {
    FX_CHECK(injector_);
    fuchsia::ui::pointerinjector::Event event;
    event.set_timestamp(0);
    {
      fuchsia::ui::pointerinjector::PointerSample pointer_sample;
      pointer_sample.set_pointer_id(kPointerId);
      pointer_sample.set_phase(phase);
      pointer_sample.set_position_in_viewport({x, y});
      fuchsia::ui::pointerinjector::Data data;
      data.set_pointer_sample(std::move(pointer_sample));
      event.set_data(std::move(data));
    }
    std::vector<fuchsia::ui::pointerinjector::Event> events;
    events.emplace_back(std::move(event));
    bool hanging_get_returned = false;
    injector_->Inject(std::move(events), [&hanging_get_returned] { hanging_get_returned = true; });
    RunLoopUntil(
        [this, &hanging_get_returned] { return hanging_get_returned || injector_channel_closed_; });
  }

  void RegisterInjector(fuchsia::ui::views::ViewRef context_view_ref,
                        fuchsia::ui::views::ViewRef target_view_ref,
                        DispatchPolicy dispatch_policy = DispatchPolicy::EXCLUSIVE_TARGET,
                        std::array<float, 9> viewport_to_context_transform = kIdentityMatrix) {
    fuchsia::ui::pointerinjector::Config config;
    config.set_device_id(kDeviceId);
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

    injector_.set_error_handler([this](zx_status_t) { injector_channel_closed_ = true; });
    bool register_callback_fired = false;
    registry_->Register(std::move(config), injector_.NewRequest(),
                        [&register_callback_fired] { register_callback_fired = true; });
    RunLoopUntil([&register_callback_fired] { return register_callback_fired; });
    ASSERT_FALSE(injector_channel_closed_);
  }

  // Starts a recursive TouchSource::Watch() loop that collects all received events into
  // |out_events|.
  void StartWatchLoop(fuchsia::ui::pointer::TouchSourcePtr& touch_source,
                      std::vector<TouchEvent>& out_events,
                      TouchResponseType response_type = TouchResponseType::MAYBE) {
    const size_t index = watch_loops_.size();
    watch_loops_.emplace_back();
    watch_loops_.at(index) = [this, &touch_source, &out_events, response_type,
                              index](std::vector<TouchEvent> events) {
      std::vector<TouchResponse> responses;
      for (auto& event : events) {
        if (event.has_pointer_sample()) {
          TouchResponse response;
          response.set_response_type(response_type);
          responses.emplace_back(std::move(response));
        } else {
          responses.emplace_back();
        }
      }
      std::move(events.begin(), events.end(), std::back_inserter(out_events));

      touch_source->Watch(std::move(responses), [this, index](std::vector<TouchEvent> events) {
        watch_loops_.at(index)(std::move(events));
      });
    };
    touch_source->Watch({}, watch_loops_.at(index));
  }

  bool injector_channel_closed_ = false;
  std::array<std::array<float, 2>, 2> FullScreenExtents() const { return {{{0, 0}, {9, 9}}}; }

  std::unique_ptr<RootSession> root_session_;

 private:
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
  fuchsia::ui::scenic::ScenicPtr scenic_;
  fuchsia::ui::pointerinjector::RegistryPtr registry_;
  fuchsia::ui::pointerinjector::DevicePtr injector_;

  // Holds watch loops so they stay alive through the duration of the test.
  std::vector<std::function<void(std::vector<TouchEvent>)>> watch_loops_;
};

// Test for checking that the pointerinjector channel is closed when context and target relationship
// in the scene graph becomes invalid for injection.
TEST_F(GfxTouchIntegrationTest, InjectorChannel_ShouldClose_WhenSceneBreaks) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up scene with two ViewHolders, one a child of the other.
  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  scenic::View view(root_session_->session, std::move(v1), std::move(root_control_ref),
                    fidl::Clone(root_view_ref), "child1_view");
  scenic::ViewHolder holder_1(root_session_->session, std::move(vh1), "holder_1");
  root_session_->scene.AddChild(holder_1);

  scenic::ViewHolder holder_2(root_session_->session, std::move(vh2), "holder_2");
  view.AddChild(holder_2);
  BlockingPresent(*root_session_->session);

  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  auto [child_session, child_touch_source] = CreateChildView(
      std::move(v2), std::move(child_control_ref), fidl::Clone(child_view_ref), "child_view");

  RegisterInjector(std::move(root_view_ref), std::move(child_view_ref));

  // Break the scene graph relation that the pointerinjector relies on. Observe the channel close.
  view.DetachChild(holder_2);
  BlockingPresent(*root_session_->session);
}

// In this test we set up the context and the target. We apply a scale, rotation and translation
// transform to both of their view holder nodes, and then inject pointer events to confirm that
// the coordinates received by the listener are correctly transformed.
// Only the transformation of the target, relative to the context, should have any effect on
// the output.
// The viewport-to-context transform here is the identity. That is, the size of the 9x9 viewport
// matches the size of the 5x5 context view.
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
// Y 4      O      O      O      O    A 1 - - - C1
// |                                  - - - - - -
// V O      O      O      O      O    - O - - - -
//                                    - - - - - -
//   O      O      O      O      O    - O - - - -
//                                    R - - - - C2
//   O      O      O      O      O      O
//
//   O      O      O      O      O      O
//
//   3      O      O      O      O      2
//
TEST_F(GfxTouchIntegrationTest, InjectedInput_ShouldBeCorrectlyTransformed) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // 90 degrees counter clockwise rotation around Z-axis (Z-axis points into screen, so appears as
  // clockwise rotation).
  const auto rotation_quaternion = glm::angleAxis(glm::pi<float>() / 2.f, glm::vec3(0, 0, 1));

  // Set up scene with two ViewHolders, one a child of the other.
  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  {
    scenic::View view(root_session_->session, std::move(v1), std::move(root_control_ref),
                      fidl::Clone(root_view_ref), "child1_view");
    scenic::ViewHolder holder_1(root_session_->session, std::move(vh1), "holder_1");
    root_session_->scene.AddChild(holder_1);
    holder_1.SetViewProperties(k5x5x1);
    // Scale, rotate and translate the context to verify that it has no effect on the outcome.
    holder_1.SetScale(2, 3, 1);
    holder_1.SetRotation(rotation_quaternion.x, rotation_quaternion.y, rotation_quaternion.z,
                         rotation_quaternion.w);
    holder_1.SetTranslation(1, 0, 0);

    scenic::ViewHolder holder_2(root_session_->session, std::move(vh2), "holder_2");
    view.AddChild(holder_2);
    holder_2.SetViewProperties(k5x5x1);
    // Scale, rotate and translate target.
    // Scale X by 2 and Y by 3.
    holder_2.SetScale(2, 3, 1);
    holder_2.SetRotation(rotation_quaternion.x, rotation_quaternion.y, rotation_quaternion.z,
                         rotation_quaternion.w);
    // Translate by 1 in the X direction.
    holder_2.SetTranslation(1, 0, 0);
    BlockingPresent(*root_session_->session);
  }

  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  auto [child_session, child_touch_source] = CreateChildView(
      std::move(v2), std::move(child_control_ref), fidl::Clone(child_view_ref), "child_view");
  child_touch_source.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Touch source closed with status: " << zx_status_get_string(status);
  });

  std::vector<TouchEvent> child_events;
  StartWatchLoop(child_touch_source, child_events);

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation.
  RegisterInjector(std::move(root_view_ref), std::move(child_view_ref));
  Inject(0, 0, fupi_EventPhase::ADD);                                   // A
  Inject(5, 0, fupi_EventPhase::CHANGE);                                // C1
  Inject(5, 5, fupi_EventPhase::CHANGE);                                // C2
  Inject(0, 5, fupi_EventPhase::REMOVE);                                // R
  RunLoopUntil([&child_events] { return child_events.size() == 4u; });  // Succeeds or times out.

  // ASSERT for existance checks to avoid UB data reads.
  ASSERT_TRUE(child_events[0].has_timestamp());
  ASSERT_TRUE(child_events[0].has_trace_flow_id());
  ASSERT_TRUE(child_events[0].has_device_info());
  ASSERT_TRUE(child_events[0].has_view_parameters());
  ASSERT_TRUE(child_events[0].has_pointer_sample());
  ASSERT_TRUE(child_events[0].has_interaction_result());

  ASSERT_TRUE(child_events[1].has_timestamp());
  ASSERT_TRUE(child_events[1].has_trace_flow_id());
  EXPECT_FALSE(child_events[1].has_device_info());
  EXPECT_FALSE(child_events[1].has_view_parameters());
  ASSERT_TRUE(child_events[1].has_pointer_sample());
  EXPECT_FALSE(child_events[1].has_interaction_result());

  ASSERT_TRUE(child_events[2].has_timestamp());
  ASSERT_TRUE(child_events[2].has_trace_flow_id());
  EXPECT_FALSE(child_events[2].has_device_info());
  EXPECT_FALSE(child_events[2].has_view_parameters());
  ASSERT_TRUE(child_events[2].has_pointer_sample());
  EXPECT_FALSE(child_events[2].has_interaction_result());

  ASSERT_TRUE(child_events[3].has_timestamp());
  ASSERT_TRUE(child_events[3].has_trace_flow_id());
  EXPECT_FALSE(child_events[3].has_device_info());
  EXPECT_FALSE(child_events[3].has_view_parameters());
  ASSERT_TRUE(child_events[3].has_pointer_sample());
  EXPECT_FALSE(child_events[3].has_interaction_result());

  {  // Check layout validity.CreateChildView
    EXPECT_EQ(child_events[0].device_info().id(), kDeviceId);
    const auto& interaction_result = child_events[0].interaction_result();
    EXPECT_EQ(interaction_result.interaction.device_id, kDeviceId);
    EXPECT_EQ(interaction_result.interaction.pointer_id, kPointerId);
    EXPECT_EQ(interaction_result.status, TouchInteractionStatus::GRANTED);
    const auto& view_parameters = child_events[0].view_parameters();
    EXPECT_THAT(view_parameters.view.min, testing::ElementsAre(0.f, 0.f));
    EXPECT_THAT(view_parameters.view.max, testing::ElementsAre(5.f, 5.f));
    EXPECT_THAT(view_parameters.viewport.min, testing::ElementsAre(0.f, 0.f));
    EXPECT_THAT(view_parameters.viewport.max, testing::ElementsAre(9.f, 9.f));
  }

  const uint32_t interaction_id = child_events[0].interaction_result().interaction.interaction_id;
  {
    const auto& pointer = child_events[0].pointer_sample();
    ASSERT_TRUE(pointer.has_interaction());
    EXPECT_TRUE(pointer.has_phase());
    EXPECT_TRUE(pointer.has_position_in_viewport());
    const auto& pointer_interaction_id = pointer.interaction();
    EXPECT_EQ(pointer_interaction_id.device_id, kDeviceId);
    EXPECT_EQ(pointer_interaction_id.pointer_id, kPointerId);
    EXPECT_EQ(pointer_interaction_id.interaction_id, interaction_id);
  }
  {
    const auto& pointer = child_events[1].pointer_sample();
    ASSERT_TRUE(pointer.has_interaction());
    EXPECT_TRUE(pointer.has_phase());
    EXPECT_TRUE(pointer.has_position_in_viewport());
    const auto& pointer_interaction_id = pointer.interaction();
    EXPECT_EQ(pointer_interaction_id.device_id, kDeviceId);
    EXPECT_EQ(pointer_interaction_id.pointer_id, kPointerId);
    EXPECT_EQ(pointer_interaction_id.interaction_id, interaction_id);
  }
  {
    const auto& pointer = child_events[2].pointer_sample();
    ASSERT_TRUE(pointer.has_interaction());
    EXPECT_TRUE(pointer.has_phase());
    EXPECT_TRUE(pointer.has_position_in_viewport());
    const auto& pointer_interaction_id = pointer.interaction();
    EXPECT_EQ(pointer_interaction_id.device_id, kDeviceId);
    EXPECT_EQ(pointer_interaction_id.pointer_id, kPointerId);
    EXPECT_EQ(pointer_interaction_id.interaction_id, interaction_id);
  }
  {
    const auto& pointer = child_events[3].pointer_sample();
    ASSERT_TRUE(pointer.has_interaction());
    EXPECT_TRUE(pointer.has_phase());
    EXPECT_TRUE(pointer.has_position_in_viewport());
    const auto& pointer_interaction_id = pointer.interaction();
    EXPECT_EQ(pointer_interaction_id.device_id, kDeviceId);
    EXPECT_EQ(pointer_interaction_id.pointer_id, kPointerId);
    EXPECT_EQ(pointer_interaction_id.interaction_id, interaction_id);
  }

  // Check pointer samples.
  {
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    EXPECT_EQ_POINTER(child_events[0].pointer_sample(), viewport_to_view_transform, EventPhase::ADD,
                      0.f / 2.f, (0.f + 1.f) / 3.f);
    EXPECT_EQ_POINTER(child_events[1].pointer_sample(), viewport_to_view_transform,
                      EventPhase::CHANGE, 0.f / 2.f, (-5.f + 1.f) / 3.f);
    EXPECT_EQ_POINTER(child_events[2].pointer_sample(), viewport_to_view_transform,
                      EventPhase::CHANGE, 5.f / 2.f, (-5.f + 1.f) / 3.f);
    EXPECT_EQ_POINTER(child_events[3].pointer_sample(), viewport_to_view_transform,
                      EventPhase::REMOVE, 5.f / 2.f, (0.f + 1.f) / 3.f);
  }
}

// In this test the context and the target have identical coordinate systems, but the viewport
// no longer matches the context's coordinate system.
//
// Below is an ASCII diagram showing the resulting setup.
// O represents the views, - the viewport.
//   X ->
// Y O   O   O   O   O   O
// |
// V   A - - - - C1- - - -
//   O - O - O - O - O - O
//     - - - - - - - - - -
//     - - - - - - - - - -
//   O - O - O - O - O - O
//     R - - - - C2- - - -
//     - - - - - - - - - -
//   O - O - O - O - O - O
//     - - - - - - - - - -
//     - - - - - - - - - -
//   O   O   O   O   O   O
//
//
//   O   O   O   O   O   O
//
TEST_F(GfxTouchIntegrationTest, InjectedInput_ShouldBeCorrectlyViewportTransformed) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up a scene with two ViewHolders, one a child of the other.
  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  {
    scenic::View view(root_session_->session, std::move(v1), std::move(root_control_ref),
                      fidl::Clone(root_view_ref), "child1_view");
    scenic::ViewHolder holder_1(root_session_->session, std::move(vh1), "holder_1");
    holder_1.SetViewProperties(k5x5x1);
    root_session_->scene.AddChild(holder_1);
    scenic::ViewHolder holder_2(root_session_->session, std::move(vh2), "holder_2");
    holder_2.SetViewProperties(k5x5x1);
    view.AddChild(holder_2);
    BlockingPresent(*root_session_->session);
  }

  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  auto [child_session, child_touch_source] = CreateChildView(
      std::move(v2), std::move(child_control_ref), fidl::Clone(child_view_ref), "child_view");
  child_touch_source.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Touch source closed with status: " << zx_status_get_string(status);
  });

  std::vector<TouchEvent> child_events;
  StartWatchLoop(child_touch_source, child_events);

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation.

  // Transform to scale the viewport by 1/2 in the x-direction, 1/3 in the y-direction,
  // and then translate by (1, 2).
  // clang-format off
  static constexpr std::array<float, 9> kViewportToContextTransform = {
    1.f/2.f,        0,  0, // first column
          0,  1.f/3.f,  0, // second column
          1,        2,  1, // third column
  };
  // clang-format on

  RegisterInjector(std::move(root_view_ref), std::move(child_view_ref),
                   DispatchPolicy::EXCLUSIVE_TARGET, kViewportToContextTransform);
  Inject(0, 0, fupi_EventPhase::ADD);                                   // A
  Inject(5, 0, fupi_EventPhase::CHANGE);                                // C1
  Inject(5, 5, fupi_EventPhase::CHANGE);                                // C2
  Inject(0, 5, fupi_EventPhase::REMOVE);                                // R
  RunLoopUntil([&child_events] { return child_events.size() == 4u; });  // Succeeds or times out.

  // Check pointer samples.
  {
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    EXPECT_EQ_POINTER(child_events[0].pointer_sample(), viewport_to_view_transform, EventPhase::ADD,
                      0.f / 2.f + 1, 0.f / 3.f + 2);
    EXPECT_EQ_POINTER(child_events[1].pointer_sample(), viewport_to_view_transform,
                      EventPhase::CHANGE, 5.f / 2.f + 1, 0.f / 3.f + 2);
    EXPECT_EQ_POINTER(child_events[2].pointer_sample(), viewport_to_view_transform,
                      EventPhase::CHANGE, 5.f / 2.f + 1, 5.f / 3.f + 2);
    EXPECT_EQ_POINTER(child_events[3].pointer_sample(), viewport_to_view_transform,
                      EventPhase::REMOVE, 0.f / 2.f + 1, 5.f / 3.f + 2);
  }
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
TEST_F(GfxTouchIntegrationTest, InjectedInput_OnRotatedChild_ShouldHitEdges) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up a scene with two ViewHolders, one a child of the other.
  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  {
    scenic::View view(root_session_->session, std::move(v1), std::move(root_control_ref),
                      fidl::Clone(root_view_ref), "child1_view");
    scenic::ViewHolder holder_1(root_session_->session, std::move(vh1), "holder_1");
    holder_1.SetViewProperties(k5x5x1);
    root_session_->scene.AddChild(holder_1);
    scenic::ViewHolder holder_2(root_session_->session, std::move(vh2), "holder_2");
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
  auto [child_session, child_touch_source] = CreateChildView(
      std::move(v2), std::move(child_control_ref), fidl::Clone(child_view_ref), "child_view");
  child_touch_source.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Touch source closed with status: " << zx_status_get_string(status);
  });

  std::vector<TouchEvent> child_events;
  StartWatchLoop(child_touch_source, child_events);

  // Scene is now set up, send in the input. One interaction for each corner.
  RegisterInjector(std::move(root_view_ref), std::move(child_view_ref),
                   DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET);
  Inject(0, 0, fupi_EventPhase::ADD);
  Inject(0, 0, fupi_EventPhase::REMOVE);
  Inject(0, 5, fupi_EventPhase::ADD);
  Inject(0, 5, fupi_EventPhase::REMOVE);
  Inject(5, 5, fupi_EventPhase::ADD);
  Inject(5, 5, fupi_EventPhase::REMOVE);
  Inject(5, 0, fupi_EventPhase::ADD);
  Inject(5, 0, fupi_EventPhase::REMOVE);
  RunLoopUntil([&child_events] { return child_events.size() == 8u; });  // Succeeds or times out.

  {  // Target should receive all events rotated 90 degrees.
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    EXPECT_EQ_POINTER(child_events[0].pointer_sample(), viewport_to_view_transform, EventPhase::ADD,
                      0.f, 5.f);
    EXPECT_EQ_POINTER(child_events[1].pointer_sample(), viewport_to_view_transform,
                      EventPhase::REMOVE, 0.f, 5.f);

    EXPECT_EQ_POINTER(child_events[2].pointer_sample(), viewport_to_view_transform, EventPhase::ADD,
                      5.f, 5.f);
    EXPECT_EQ_POINTER(child_events[3].pointer_sample(), viewport_to_view_transform,
                      EventPhase::REMOVE, 5.f, 5.f);

    EXPECT_EQ_POINTER(child_events[4].pointer_sample(), viewport_to_view_transform, EventPhase::ADD,
                      5.f, 0.f);
    EXPECT_EQ_POINTER(child_events[5].pointer_sample(), viewport_to_view_transform,
                      EventPhase::REMOVE, 5.f, 0.f);

    EXPECT_EQ_POINTER(child_events[6].pointer_sample(), viewport_to_view_transform, EventPhase::ADD,
                      0.f, 0.f);
    EXPECT_EQ_POINTER(child_events[7].pointer_sample(), viewport_to_view_transform,
                      EventPhase::REMOVE, 0.f, 0.f);
  }
}

// In this test we set up the context and the target. We apply clip space transform to the camera
// and then inject pointer events to confirm that the coordinates received by the listener are
// not impacted by the clip space transform.
TEST_F(GfxTouchIntegrationTest, ClipSpaceTransformedScene_ShouldHaveNoImpactOnOutput) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set the clip space transform on the camera.
  // Camera zooms in by 3x, and the camera is moved to (24,54) in the scene's coordinate space.
  root_session_->camera.SetClipSpaceTransform(/*x offset*/ 24, /*y offset*/ 54, /*scale*/ 3);

  // Set up a scene with two ViewHolders, one a child of the other.
  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  {
    scenic::View view(root_session_->session, std::move(v1), std::move(root_control_ref),
                      fidl::Clone(root_view_ref), "child1_view");
    scenic::ViewHolder holder_1(root_session_->session, std::move(vh1), "holder_1");
    holder_1.SetViewProperties(k5x5x1);
    root_session_->scene.AddChild(holder_1);
    scenic::ViewHolder holder_2(root_session_->session, std::move(vh2), "holder_2");
    holder_2.SetViewProperties(k5x5x1);
    view.AddChild(holder_2);
    BlockingPresent(*root_session_->session);
  }

  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  auto [child_session, child_touch_source] = CreateChildView(
      std::move(v2), std::move(child_control_ref), fidl::Clone(child_view_ref), "child_view");
  child_touch_source.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Touch source closed with status: " << zx_status_get_string(status);
  });

  std::vector<TouchEvent> child_events;
  StartWatchLoop(child_touch_source, child_events);

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation.
  RegisterInjector(std::move(root_view_ref), std::move(child_view_ref));
  Inject(0, 0, fupi_EventPhase::ADD);
  Inject(5, 0, fupi_EventPhase::CHANGE);
  Inject(5, 5, fupi_EventPhase::CHANGE);
  Inject(0, 5, fupi_EventPhase::REMOVE);
  RunLoopUntil([&child_events] { return child_events.size() == 4u; });  // Succeeds or times out.

  // Target should receive identical events to injected, since their coordinate spaces are the same.
  {
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    EXPECT_EQ_POINTER(child_events[0].pointer_sample(), viewport_to_view_transform, EventPhase::ADD,
                      0.f, 0.f);
    EXPECT_EQ_POINTER(child_events[1].pointer_sample(), viewport_to_view_transform,
                      EventPhase::CHANGE, 5.f, 0.f);
    EXPECT_EQ_POINTER(child_events[2].pointer_sample(), viewport_to_view_transform,
                      EventPhase::CHANGE, 5.f, 5.f);
    EXPECT_EQ_POINTER(child_events[3].pointer_sample(), viewport_to_view_transform,
                      EventPhase::REMOVE, 0.f, 5.f);
  }
}

// Basic scene (no transformations) where the Viewport is smaller than the Views.
// We then inject two streams: The first has an ADD outside the Viewport, which counts as a miss and
// should not be seen by anyone. The second stream has the ADD inside the Viewport and subsequent
// events outside, and this full stream should be seen by the target.
TEST_F(GfxTouchIntegrationTest, InjectionOutsideViewport_ShouldLimitOnADD) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up a scene with two ViewHolders, one a child of the other. Make the Views bigger than the\
  // Viewport.
  static constexpr fuchsia::ui::gfx::ViewProperties k100x100x1 = {
      .bounding_box = {.max = {100, 100, 1}}};
  // Set up a scene with two ViewHolders, one a child of the other.
  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  {
    scenic::View view(root_session_->session, std::move(v1), std::move(root_control_ref),
                      fidl::Clone(root_view_ref), "child1_view");
    scenic::ViewHolder holder_1(root_session_->session, std::move(vh1), "holder_1");
    holder_1.SetViewProperties(k100x100x1);
    root_session_->scene.AddChild(holder_1);
    scenic::ViewHolder holder_2(root_session_->session, std::move(vh2), "holder_2");
    holder_2.SetViewProperties(k100x100x1);
    view.AddChild(holder_2);
    BlockingPresent(*root_session_->session);
  }

  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  auto [child_session, child_touch_source] = CreateChildView(
      std::move(v2), std::move(child_control_ref), fidl::Clone(child_view_ref), "child_view");
  child_touch_source.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Touch source closed with status: " << zx_status_get_string(status);
  });

  std::vector<TouchEvent> child_events;
  StartWatchLoop(child_touch_source, child_events);

  // Scene is now set up, send in the input. The initial input is outside the viewport and
  // the stream should therefore not be seen by anyone.
  RegisterInjector(std::move(root_view_ref), std::move(child_view_ref),
                   DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET);
  Inject(10, 10, fupi_EventPhase::ADD);  // Outside viewport.
  // Rest inside viewport, but should not be delivered.
  Inject(5, 0, fupi_EventPhase::CHANGE);
  Inject(5, 5, fupi_EventPhase::CHANGE);
  Inject(0, 5, fupi_EventPhase::REMOVE);

  // Send in input starting in the viewport and moving outside.
  Inject(1, 1, fupi_EventPhase::ADD);  // Inside viewport.
  // Rest outside viewport, but should still be delivered.
  Inject(50, 0, fupi_EventPhase::CHANGE);
  Inject(50, 50, fupi_EventPhase::CHANGE);
  Inject(0, 50, fupi_EventPhase::REMOVE);
  RunLoopUntil([&child_events] { return child_events.size() >= 4u; });  // Succeeds or times out.
  EXPECT_EQ(child_events.size(), 4u);

  {
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    EXPECT_EQ_POINTER(child_events[0].pointer_sample(), viewport_to_view_transform, EventPhase::ADD,
                      1.f, 1.f);
    EXPECT_EQ_POINTER(child_events[1].pointer_sample(), viewport_to_view_transform,
                      EventPhase::CHANGE, 50.f, 0.f);
    EXPECT_EQ_POINTER(child_events[2].pointer_sample(), viewport_to_view_transform,
                      EventPhase::CHANGE, 50.f, 50.f);
    EXPECT_EQ_POINTER(child_events[3].pointer_sample(), viewport_to_view_transform,
                      EventPhase::REMOVE, 0.f, 50.f);
  }
}

TEST_F(GfxTouchIntegrationTest, Exclusive_TargetDisconnectedMidStream_ShouldCancelStream) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up a scene with two ViewHolders, one a child of the other.
  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  scenic::View view(root_session_->session, std::move(v1), std::move(root_control_ref),
                    fidl::Clone(root_view_ref), "root_view");
  scenic::ViewHolder holder_1(root_session_->session, std::move(vh1), "holder_1");
  holder_1.SetViewProperties(k5x5x1);
  root_session_->scene.AddChild(holder_1);
  scenic::ViewHolder holder_2(root_session_->session, std::move(vh2), "holder_2");
  holder_2.SetViewProperties(k5x5x1);
  view.AddChild(holder_2);
  BlockingPresent(*root_session_->session);

  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  auto [child_session, child_touch_source] = CreateChildView(
      std::move(v2), std::move(child_control_ref), fidl::Clone(child_view_ref), "child_view");
  child_touch_source.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Touch source closed with status: " << zx_status_get_string(status);
  });

  std::vector<TouchEvent> child_events;
  StartWatchLoop(child_touch_source, child_events);

  // Send in the first events of a stream.
  RegisterInjector(std::move(root_view_ref), std::move(child_view_ref));
  Inject(0, 0, fupi_EventPhase::ADD);
  Inject(5, 0, fupi_EventPhase::CHANGE);
  RunLoopUntil([&child_events] { return child_events.size() >= 2u; });  // Succeeds or times out.
  EXPECT_EQ(child_events.size(), 2u);

  // Disrupt the scene graph.
  view.DetachChild(holder_2);
  BlockingPresent(*root_session_->session);

  // Next event should deliver a cancel event to the child (and close the injector since it's the
  // target)
  Inject(5, 5, fupi_EventPhase::CHANGE);
  EXPECT_TRUE(injector_channel_closed_);
  RunLoopUntil([&child_events] { return child_events.size() >= 3u; });  // Succeeds or times out.

  {
    ASSERT_EQ(child_events.size(), 3u);
    ASSERT_TRUE(child_events.back().has_pointer_sample());
    const auto& sample = child_events.back().pointer_sample();
    ASSERT_TRUE(sample.has_phase());
    EXPECT_EQ(sample.phase(), EventPhase::CANCEL);
  }
}

TEST_F(GfxTouchIntegrationTest, Exclusive_TargetDyingMidStream_ShouldKillChannel) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up a scene with two ViewHolders, one a child of the other.
  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  {
    scenic::View view(root_session_->session, std::move(v1), std::move(root_control_ref),
                      fidl::Clone(root_view_ref), "root_view");
    scenic::ViewHolder holder_1(root_session_->session, std::move(vh1), "holder_1");
    holder_1.SetViewProperties(k5x5x1);
    root_session_->scene.AddChild(holder_1);
    scenic::ViewHolder holder_2(root_session_->session, std::move(vh2), "holder_2");
    holder_2.SetViewProperties(k5x5x1);
    view.AddChild(holder_2);
    BlockingPresent(*root_session_->session);
  }

  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  auto [child_session, child_touch_source] = CreateChildView(
      std::move(v2), std::move(child_control_ref), fidl::Clone(child_view_ref), "child_view");
  child_touch_source.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Touch source closed with status: " << zx_status_get_string(status);
  });

  std::vector<TouchEvent> child_events;
  StartWatchLoop(child_touch_source, child_events);

  // Send in the first events of a stream.
  RegisterInjector(std::move(root_view_ref), std::move(child_view_ref));
  Inject(0, 0, fupi_EventPhase::ADD);
  Inject(5, 0, fupi_EventPhase::CHANGE);
  RunLoopUntil([&child_events] { return child_events.size() >= 2u; });  // Succeeds or times out.
  EXPECT_EQ(child_events.size(), 2u);

  // Kill the child.
  bool child_view_died = false;
  root_session_->session->set_event_handler(
      [&child_view_died](std::vector<fuchsia::ui::scenic::Event> events) {
        for (const auto& event : events) {
          if (event.is_gfx() && event.gfx().is_view_disconnected())
            child_view_died = true;
        }
      });
  child_touch_source.Unbind();
  child_session.reset();
  RunLoopUntil([&child_view_died] { return child_view_died; });

  // TODO(fxbug.dev/81683): We perform one more present to avoid flakes. Session death causes view
  // disconnected signals to be sent out-of-sync with the normal Present flow.
  BlockingPresent(*root_session_->session);

  // Next injection should close the channel.
  Inject(0, 5, fupi_EventPhase::CHANGE);
  EXPECT_TRUE(injector_channel_closed_);
}

// Sets up a scene with three views: Root -> Child1 -> Child2.
// Injects in HitTest mode, all events delivered to Child1 and Child2.
// Disconnects Child2 and observes loss from Child2.
TEST_F(GfxTouchIntegrationTest, HitTested_ViewDisconnectedMidContest_ShouldLoseContest) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  {
    scenic::View view(root_session_->session, std::move(v1), std::move(root_control_ref),
                      fidl::Clone(root_view_ref), "root_view");
    scenic::ViewHolder holder_1(root_session_->session, std::move(vh1), "holder_1");
    holder_1.SetViewProperties(k5x5x1);
    root_session_->scene.AddChild(holder_1);
    scenic::ViewHolder child1_holder(root_session_->session, std::move(vh2), "child1_holder");
    child1_holder.SetViewProperties(k5x5x1);
    view.AddChild(child1_holder);
    BlockingPresent(*root_session_->session);
  }

  auto [child1_control_ref, child1_view_ref] = scenic::ViewRefPair::New();
  auto [child1_session, child1_touch_source] = CreateSessionWithTouchSource(scenic());
  auto [v3, vh3] = scenic::ViewTokenPair::New();
  scenic::View view(child1_session.get(), std::move(v2), std::move(child1_control_ref),
                    fidl::Clone(child1_view_ref), "child1_view");
  scenic::ViewHolder child2_holder(child1_session.get(), std::move(vh3), "child2_holder");
  child2_holder.SetViewProperties(k5x5x1);
  view.AddChild(child2_holder);
  BlockingPresent(*child1_session);
  child1_touch_source.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Touch source closed with status: " << zx_status_get_string(status);
  });

  auto [child2_control_ref, child2_view_ref] = scenic::ViewRefPair::New();
  auto [child2_session, child2_touch_source] = CreateChildView(
      std::move(v3), std::move(child2_control_ref), fidl::Clone(child2_view_ref), "child2_view");
  child2_touch_source.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Touch source closed with status: " << zx_status_get_string(status);
  });

  std::vector<TouchEvent> child1_events;
  StartWatchLoop(child1_touch_source, child1_events);
  std::vector<TouchEvent> child2_events;
  StartWatchLoop(child2_touch_source, child2_events);

  // Send in the first events of a stream.
  RegisterInjector(std::move(root_view_ref), std::move(child1_view_ref),
                   DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET);
  Inject(0, 0, fupi_EventPhase::ADD);
  Inject(5, 0, fupi_EventPhase::CHANGE);
  RunLoopUntil([&child2_events] { return child2_events.size() == 2u; });  // Succeeds or times out.
  EXPECT_EQ(child2_events.size(), 2u);

  // Detach view Child2 from the scene graph.
  view.DetachChild(child2_holder);
  BlockingPresent(*child1_session);

  // Next event should deliver cause child 2 to lose the contest and child 1 to win.
  Inject(5, 5, fupi_EventPhase::CHANGE);
  Inject(0, 5, fupi_EventPhase::CHANGE);
  RunLoopUntil([&child2_events] { return child2_events.size() == 3u; });  // Succeeds or times out.
  RunLoopUntil([&child1_events] { return child1_events.size() == 5u; });  // Succeeds or times out.

  ASSERT_EQ(child2_events.size(), 3u);
  ASSERT_TRUE(child2_events.back().has_interaction_result());
  EXPECT_EQ(child2_events.back().interaction_result().status, TouchInteractionStatus::DENIED);

  EXPECT_EQ(child1_events.size(), 5u);
  EXPECT_TRUE(std::any_of(child1_events.begin(), child1_events.end(), [](const TouchEvent& event) {
    return event.has_interaction_result() &&
           event.interaction_result().status == TouchInteractionStatus::GRANTED;
  }));
}

// Sets up a scene with three views: Root -> Child1 -> Child2.
// Injects in HitTest mode, all events delivered to Child1 and Child2.
// Child2 wins the contest.
// Disconnects Child2 and observes cancel event delivered to Child2.
TEST_F(GfxTouchIntegrationTest, HitTested_ViewDisconnectedAfterWinning_ShouldCancelStream) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  {
    scenic::View view(root_session_->session, std::move(v1), std::move(root_control_ref),
                      fidl::Clone(root_view_ref), "root_view");
    scenic::ViewHolder holder_1(root_session_->session, std::move(vh1), "holder_1");
    holder_1.SetViewProperties(k5x5x1);
    root_session_->scene.AddChild(holder_1);
    scenic::ViewHolder child1_holder(root_session_->session, std::move(vh2), "child1_holder");
    child1_holder.SetViewProperties(k5x5x1);
    view.AddChild(child1_holder);
    BlockingPresent(*root_session_->session);
  }

  auto [child1_control_ref, child1_view_ref] = scenic::ViewRefPair::New();
  auto [child1_session, child1_touch_source] = CreateSessionWithTouchSource(scenic());
  auto [v3, vh3] = scenic::ViewTokenPair::New();
  scenic::View view(child1_session.get(), std::move(v2), std::move(child1_control_ref),
                    fidl::Clone(child1_view_ref), "child1_view");
  scenic::ViewHolder child2_holder(child1_session.get(), std::move(vh3), "child2_holder");
  child2_holder.SetViewProperties(k5x5x1);
  view.AddChild(child2_holder);
  BlockingPresent(*child1_session);
  child1_touch_source.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Touch source closed with status: " << zx_status_get_string(status);
  });

  auto [child2_control_ref, child2_view_ref] = scenic::ViewRefPair::New();
  auto [child2_session, child2_touch_source] = CreateChildView(
      std::move(v3), std::move(child2_control_ref), fidl::Clone(child2_view_ref), "child2_view");
  child2_touch_source.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Touch source closed with status: " << zx_status_get_string(status);
  });

  std::vector<TouchEvent> child1_events;
  StartWatchLoop(child1_touch_source, child1_events, TouchResponseType::NO);
  std::vector<TouchEvent> child2_events;
  StartWatchLoop(child2_touch_source, child2_events);

  // Send in the first events of a stream.
  RegisterInjector(std::move(root_view_ref), std::move(child1_view_ref),
                   DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET);
  Inject(0, 0, fupi_EventPhase::ADD);
  Inject(5, 0, fupi_EventPhase::CHANGE);

  // Child2 should win the contest.
  RunLoopUntil([&child2_events] { return child2_events.size() >= 3u; });  // Succeeds or times out.
  ASSERT_EQ(child2_events.size(), 3u);
  EXPECT_TRUE(std::any_of(child2_events.begin(), child2_events.end(), [](const TouchEvent& event) {
    return event.has_interaction_result() &&
           event.interaction_result().status == TouchInteractionStatus::GRANTED;
  }));

  // Detach view Child2 from the scene graph.
  view.DetachChild(child2_holder);
  BlockingPresent(*child1_session);

  // Next event should deliver CANCEL to Child2.
  Inject(5, 5, fupi_EventPhase::CHANGE);
  RunLoopUntil([&child2_events] { return child2_events.size() >= 4u; });  // Succeeds or times out.
  ASSERT_EQ(child2_events.size(), 4u);
  ASSERT_TRUE(child2_events.back().has_pointer_sample());
  ASSERT_TRUE(child2_events.back().pointer_sample().has_phase());
  EXPECT_EQ(child2_events.back().pointer_sample().phase(), EventPhase::CANCEL);

  // Future injections should be ignored.
  child1_events.clear();
  child2_events.clear();
  Inject(0, 5, fupi_EventPhase::CHANGE);
  EXPECT_TRUE(child1_events.empty());
  EXPECT_TRUE(child2_events.empty());
}

}  // namespace integration_tests
