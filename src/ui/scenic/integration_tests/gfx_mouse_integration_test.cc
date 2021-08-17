// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/lifecycle/cpp/fidl.h>
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
// - Dispatch done to fuchsia.ui.pointer.MouseSource in receiver View Space.

// Macros for calling EXPECT on fuchsia::ui::pointer::MousePointerSample.
// Delegates to ExpectEqualPointer(), but are macros to ensure we get the correct line number for
// the error.
#define EXPECT_EQ_POINTER_WITH_SCROLL_AND_BUTTONS(pointer_sample, viewport_to_view_transform, \
                                                  expected_x, expected_y, expected_scroll_v,  \
                                                  expected_scroll_h, expected_buttons)        \
  ExpectEqualPointer(pointer_sample, viewport_to_view_transform, expected_x, expected_y,      \
                     expected_scroll_v, expected_scroll_h, expected_buttons, __LINE__);

#define EXPECT_EQ_POINTER_WITH_SCROLL(pointer_sample, viewport_to_view_transform, expected_x, \
                                      expected_y, expected_scroll_v, expected_scroll_h)       \
  EXPECT_EQ_POINTER_WITH_SCROLL_AND_BUTTONS(pointer_sample, viewport_to_view_transform,       \
                                            expected_x, expected_y, expected_scroll_v,        \
                                            expected_scroll_h, std::vector<uint8_t>());

#define EXPECT_EQ_POINTER_WITH_BUTTONS(pointer_sample, viewport_to_view_transform, expected_x, \
                                       expected_y, expected_buttons)                           \
  EXPECT_EQ_POINTER_WITH_SCROLL_AND_BUTTONS(pointer_sample, viewport_to_view_transform,        \
                                            expected_x, expected_y, std::optional<int64_t>(),  \
                                            std::optional<int64_t>(), expected_buttons);

#define EXPECT_EQ_POINTER(pointer_sample, viewport_to_view_transform, expected_x, expected_y) \
  EXPECT_EQ_POINTER_WITH_BUTTONS(pointer_sample, viewport_to_view_transform, expected_x,      \
                                 expected_y, std::vector<uint8_t>());

namespace integration_tests {

using fuchsia::ui::pointer::MouseEvent;
using fuchsia::ui::pointer::MouseViewStatus;
using fuchsia::ui::pointerinjector::EventPhase;
using fuchsia::ui::views::ViewRef;

namespace {

const std::map<std::string, std::string> LocalServices() {
  return {{"fuchsia.ui.composition.Allocator",
           "fuchsia-pkg://fuchsia.com/gfx_integration_tests#meta/scenic.cmx"},
          {"fuchsia.ui.scenic.Scenic",
           "fuchsia-pkg://fuchsia.com/gfx_integration_tests#meta/scenic.cmx"},
          {"fuchsia.ui.pointerinjector.Registry",
           "fuchsia-pkg://fuchsia.com/gfx_integration_tests#meta/scenic.cmx"},
          // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
          {"fuchsia.ui.lifecycle.LifecycleController",
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

void ExpectEqualPointer(const fuchsia::ui::pointer::MousePointerSample& pointer_sample,
                        const std::array<float, 9>& viewport_to_view_transform, float expected_x,
                        float expected_y, std::optional<int64_t> expected_scroll_v,
                        std::optional<int64_t> expected_scroll_h,
                        std::vector<uint8_t> expected_buttons, uint32_t line_number) {
  constexpr float kEpsilon = std::numeric_limits<float>::epsilon() * 1000;
  const glm::mat3 transform_matrix = ArrayToMat3(viewport_to_view_transform);
  const std::array<float, 2> transformed_pointer =
      TransformPointerCoords(pointer_sample.position_in_viewport(), transform_matrix);
  EXPECT_NEAR(transformed_pointer[0], expected_x, kEpsilon) << "Line: " << line_number;
  EXPECT_NEAR(transformed_pointer[1], expected_y, kEpsilon) << "Line: " << line_number;
  if (expected_scroll_v.has_value()) {
    ASSERT_TRUE(pointer_sample.has_scroll_v()) << "Line: " << line_number;
    EXPECT_EQ(pointer_sample.scroll_v(), expected_scroll_v.value()) << "Line: " << line_number;
  } else {
    EXPECT_FALSE(pointer_sample.has_scroll_v()) << "Line: " << line_number;
  }
  if (expected_scroll_h.has_value()) {
    ASSERT_TRUE(pointer_sample.has_scroll_h()) << "Line: " << line_number;
    EXPECT_EQ(pointer_sample.scroll_h(), expected_scroll_h.value()) << "Line: " << line_number;
  } else {
    EXPECT_FALSE(pointer_sample.has_scroll_h()) << "Line: " << line_number;
  }
  if (expected_buttons.empty()) {
    EXPECT_FALSE(pointer_sample.has_pressed_buttons()) << "Line: " << line_number;
  } else {
    ASSERT_TRUE(pointer_sample.has_pressed_buttons()) << "Line: " << line_number;
    EXPECT_THAT(pointer_sample.pressed_buttons(), testing::ElementsAreArray(expected_buttons))
        << "Line: " << line_number;
  }
}

struct SessionWithMouseSource {
  std::unique_ptr<scenic::Session> session;
  fuchsia::ui::pointer::MouseSourcePtr mouse_source_ptr;
};

SessionWithMouseSource CreateSessionWithMouseSource(fuchsia::ui::scenic::Scenic* scenic) {
  SessionWithMouseSource session_with_mouse_source;

  fuchsia::ui::scenic::SessionEndpoints endpoints;
  fuchsia::ui::scenic::SessionPtr session_ptr;
  fuchsia::ui::scenic::SessionListenerHandle listener_handle;
  auto listener_request = listener_handle.NewRequest();
  endpoints.set_session(session_ptr.NewRequest());
  endpoints.set_session_listener(std::move(listener_handle));
  endpoints.set_mouse_source(session_with_mouse_source.mouse_source_ptr.NewRequest());
  scenic->CreateSessionT(std::move(endpoints), [] {});

  session_with_mouse_source.session =
      std::make_unique<scenic::Session>(std::move(session_ptr), std::move(listener_request));
  return session_with_mouse_source;
}

// Sets up the root of a scene.
// Present() must be called separately by the creator, since this does not have access to the
// looper.
struct RootSession {
  RootSession(fuchsia::ui::scenic::Scenic* scenic)
      : session_with_mouse_source(CreateSessionWithMouseSource(scenic)),
        session(session_with_mouse_source.session.get()),
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

  SessionWithMouseSource session_with_mouse_source;
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

class GfxMouseIntegrationTest : public gtest::TestWithEnvironmentFixture {
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

    environment_ =
        CreateNewEnclosingEnvironment("gfx_mouse_integration_test_environment", CreateServices());
    WaitForEnclosingEnvToStart(environment_.get());

    // Connects to scenic lifecycle controller in order to shutdown scenic at the end of the test.
    // This ensures the correct ordering of shutdown under CFv1: first scenic, then the fake display
    // controller.
    //
    // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
    environment_->ConnectToService<fuchsia::ui::lifecycle::LifecycleController>(
        scenic_lifecycle_controller_.NewRequest());

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

  void TearDown() override {
    // Avoid spurious errors since we are about to kill scenic.
    //
    // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
    registry_.set_error_handler(nullptr);
    scenic_.set_error_handler(nullptr);

    zx_status_t terminate_status = scenic_lifecycle_controller_->Terminate();
    FX_CHECK(terminate_status == ZX_OK)
        << "Failed to terminate Scenic with status: " << zx_status_get_string(terminate_status);
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

  SessionWithMouseSource CreateChildView(fuchsia::ui::views::ViewToken view_token,
                                         fuchsia::ui::views::ViewRefControl control_ref,
                                         fuchsia::ui::views::ViewRef view_ref,
                                         std::string debug_name) {
    auto session_with_mouse_source = CreateSessionWithMouseSource(scenic());
    scenic::Session* session = session_with_mouse_source.session.get();
    scenic::View view(session, std::move(view_token), std::move(control_ref), std::move(view_ref),
                      debug_name);
    scenic::ShapeNode shape(session);
    scenic::Rectangle rec(session, 5, 5);
    shape.SetTranslation(2.5f, 2.5f, 0);  // Center the shape within the View.
    view.AddChild(shape);
    shape.SetShape(rec);
    BlockingPresent(*session);

    return session_with_mouse_source;
  }

  void Inject(float x, float y, EventPhase phase, std::vector<uint8_t> pressed_buttons = {},
              std::optional<int64_t> scroll_v = std::nullopt,
              std::optional<int64_t> scroll_h = std::nullopt) {
    FX_CHECK(injector_);
    fuchsia::ui::pointerinjector::Event event;
    event.set_timestamp(0);
    {
      fuchsia::ui::pointerinjector::PointerSample pointer_sample;
      pointer_sample.set_pointer_id(kPointerId);
      pointer_sample.set_phase(phase);
      pointer_sample.set_position_in_viewport({x, y});
      if (scroll_v.has_value()) {
        pointer_sample.set_scroll_v(scroll_v.value());
      }
      if (scroll_h.has_value()) {
        pointer_sample.set_scroll_h(scroll_h.value());
      }
      if (!pressed_buttons.empty()) {
        pointer_sample.set_pressed_buttons(pressed_buttons);
      }
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
                        std::vector<uint8_t> buttons = {},
                        std::array<float, 9> viewport_to_context_transform = kIdentityMatrix) {
    fuchsia::ui::pointerinjector::Config config;
    config.set_device_id(kDeviceId);
    config.set_device_type(fuchsia::ui::pointerinjector::DeviceType::MOUSE);
    config.set_dispatch_policy(dispatch_policy);
    if (!buttons.empty()) {
      config.set_buttons(buttons);
    }
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

  // Starts a recursive MouseSource::Watch() loop that collects all received events into
  // |out_events|.
  void StartWatchLoop(fuchsia::ui::pointer::MouseSourcePtr& mouse_source,
                      std::vector<MouseEvent>& out_events) {
    const size_t index = watch_loops_.size();
    watch_loops_.emplace_back();
    watch_loops_.at(index) = [this, &mouse_source, &out_events,
                              index](std::vector<MouseEvent> events) {
      std::move(events.begin(), events.end(), std::back_inserter(out_events));
      mouse_source->Watch([this, index](std::vector<MouseEvent> events) {
        watch_loops_.at(index)(std::move(events));
      });
    };
    mouse_source->Watch(watch_loops_.at(index));
  }

  std::array<std::array<float, 2>, 2> FullScreenExtents() const { return {{{0, 0}, {9, 9}}}; }

  std::unique_ptr<RootSession> root_session_;
  bool injector_channel_closed_ = false;

 private:
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
  fuchsia::ui::lifecycle::LifecycleControllerSyncPtr scenic_lifecycle_controller_;
  fuchsia::ui::scenic::ScenicPtr scenic_;
  fuchsia::ui::pointerinjector::RegistryPtr registry_;
  fuchsia::ui::pointerinjector::DevicePtr injector_;

  // Holds watch loops so they stay alive through the duration of the test.
  std::vector<std::function<void(std::vector<MouseEvent>)>> watch_loops_;
};

// Test for checking that the pointerinjector channel is closed when context and target relationship
// in the scene graph becomes invalid for injection.
TEST_F(GfxMouseIntegrationTest, InjectorChannel_ShouldClose_WhenSceneBreaks) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up scene with two ViewHolders, one a child of the other.
  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  scenic::View view(root_session_->session, std::move(v1), std::move(root_control_ref),
                    fidl::Clone(root_view_ref), "root_view");
  scenic::ViewHolder holder_1(root_session_->session, std::move(vh1), "holder_1");
  root_session_->scene.AddChild(holder_1);

  scenic::ViewHolder holder_2(root_session_->session, std::move(vh2), "holder_2");
  view.AddChild(holder_2);
  BlockingPresent(*root_session_->session);

  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  auto [child_session, child_mouse_source] = CreateChildView(
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
TEST_F(GfxMouseIntegrationTest, InjectedInput_ShouldBeCorrectlyTransformed) {
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
  auto [child_session, child_mouse_source] = CreateChildView(
      std::move(v2), std::move(child_control_ref), fidl::Clone(child_view_ref), "child_view");

  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation.
  RegisterInjector(std::move(root_view_ref), std::move(child_view_ref));
  Inject(0, 0, EventPhase::ADD);                                        // A
  Inject(5, 0, EventPhase::CHANGE);                                     // C1
  Inject(5, 5, EventPhase::CHANGE);                                     // C2
  Inject(0, 5, EventPhase::CHANGE);                                     // R
  RunLoopUntil([&child_events] { return child_events.size() == 4u; });  // Succeeds or times out.

  // ASSERT for existence checks to avoid UB data reads.
  ASSERT_TRUE(child_events[0].has_timestamp());
  ASSERT_TRUE(child_events[0].has_trace_flow_id());
  ASSERT_TRUE(child_events[0].has_device_info());
  ASSERT_TRUE(child_events[0].has_view_parameters());
  ASSERT_TRUE(child_events[0].has_pointer_sample());

  ASSERT_TRUE(child_events[1].has_timestamp());
  ASSERT_TRUE(child_events[1].has_trace_flow_id());
  EXPECT_FALSE(child_events[1].has_device_info());
  EXPECT_FALSE(child_events[1].has_view_parameters());
  ASSERT_TRUE(child_events[1].has_pointer_sample());

  ASSERT_TRUE(child_events[2].has_timestamp());
  ASSERT_TRUE(child_events[2].has_trace_flow_id());
  EXPECT_FALSE(child_events[2].has_device_info());
  EXPECT_FALSE(child_events[2].has_view_parameters());
  ASSERT_TRUE(child_events[2].has_pointer_sample());

  ASSERT_TRUE(child_events[3].has_timestamp());
  ASSERT_TRUE(child_events[3].has_trace_flow_id());
  EXPECT_FALSE(child_events[3].has_device_info());
  EXPECT_FALSE(child_events[3].has_view_parameters());
  ASSERT_TRUE(child_events[3].has_pointer_sample());

  {  // Check layout validity.
    EXPECT_EQ(child_events[0].device_info().id(), kDeviceId);
    const auto& view_parameters = child_events[0].view_parameters();
    EXPECT_THAT(view_parameters.view.min, testing::ElementsAre(0.f, 0.f));
    EXPECT_THAT(view_parameters.view.max, testing::ElementsAre(5.f, 5.f));
    EXPECT_THAT(view_parameters.viewport.min, testing::ElementsAre(0.f, 0.f));
    EXPECT_THAT(view_parameters.viewport.max, testing::ElementsAre(9.f, 9.f));
  }

  {
    const auto& pointer = child_events[0].pointer_sample();
    EXPECT_TRUE(pointer.has_position_in_viewport());
  }
  {
    const auto& pointer = child_events[1].pointer_sample();
    EXPECT_TRUE(pointer.has_position_in_viewport());
  }
  {
    const auto& pointer = child_events[2].pointer_sample();
    EXPECT_TRUE(pointer.has_position_in_viewport());
  }
  {
    const auto& pointer = child_events[3].pointer_sample();
    EXPECT_TRUE(pointer.has_position_in_viewport());
  }

  // Check pointer samples.
  {
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    EXPECT_EQ_POINTER(child_events[0].pointer_sample(), viewport_to_view_transform, 0.f / 2.f,
                      (0.f + 1.f) / 3.f);
    EXPECT_EQ_POINTER(child_events[1].pointer_sample(), viewport_to_view_transform, 0.f / 2.f,
                      (-5.f + 1.f) / 3.f);
    EXPECT_EQ_POINTER(child_events[2].pointer_sample(), viewport_to_view_transform, 5.f / 2.f,
                      (-5.f + 1.f) / 3.f);
    EXPECT_EQ_POINTER(child_events[3].pointer_sample(), viewport_to_view_transform, 5.f / 2.f,
                      (0.f + 1.f) / 3.f);
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
TEST_F(GfxMouseIntegrationTest, InjectedInput_ShouldBeCorrectlyViewportTransformed) {
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
  auto [child_session, child_mouse_source] = CreateChildView(
      std::move(v2), std::move(child_control_ref), fidl::Clone(child_view_ref), "child_view");
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

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
                   fuchsia::ui::pointerinjector::DispatchPolicy::EXCLUSIVE_TARGET, {},
                   kViewportToContextTransform);
  Inject(0, 0, EventPhase::ADD);                                        // A
  Inject(5, 0, EventPhase::CHANGE);                                     // C1
  Inject(5, 5, EventPhase::CHANGE);                                     // C2
  Inject(0, 5, EventPhase::CHANGE);                                     // R
  RunLoopUntil([&child_events] { return child_events.size() == 4u; });  // Succeeds or times out.

  // Check pointer samples.
  {
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    EXPECT_EQ_POINTER(child_events[0].pointer_sample(), viewport_to_view_transform, 0.f / 2.f + 1,
                      0.f / 3.f + 2);
    EXPECT_EQ_POINTER(child_events[1].pointer_sample(), viewport_to_view_transform, 5.f / 2.f + 1,
                      0.f / 3.f + 2);
    EXPECT_EQ_POINTER(child_events[2].pointer_sample(), viewport_to_view_transform, 5.f / 2.f + 1,
                      5.f / 3.f + 2);
    EXPECT_EQ_POINTER(child_events[3].pointer_sample(), viewport_to_view_transform, 0.f / 2.f + 1,
                      5.f / 3.f + 2);
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
TEST_F(GfxMouseIntegrationTest, InjectedInput_OnRotatedChild_ShouldHitEdges) {
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
  auto [child_session, child_mouse_source] = CreateChildView(
      std::move(v2), std::move(child_control_ref), fidl::Clone(child_view_ref), "child_view");
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Scene is now set up, send in the input. One interaction for each corner.
  RegisterInjector(std::move(root_view_ref), std::move(child_view_ref),
                   fuchsia::ui::pointerinjector::DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET);
  Inject(0, 0, EventPhase::ADD);
  Inject(0, 5, EventPhase::CHANGE);
  Inject(5, 5, EventPhase::CHANGE);
  Inject(5, 0, EventPhase::CHANGE);
  RunLoopUntil([&child_events] { return child_events.size() == 4u; });  // Succeeds or times out.

  {  // Target should receive all events rotated 90 degrees.
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    EXPECT_EQ_POINTER(child_events[0].pointer_sample(), viewport_to_view_transform, 0.f, 5.f);
    ASSERT_TRUE(child_events[0].has_stream_info());
    EXPECT_EQ(child_events[0].stream_info().status, MouseViewStatus::ENTERED);
    EXPECT_EQ_POINTER(child_events[1].pointer_sample(), viewport_to_view_transform, 5.f, 5.f);
    EXPECT_FALSE(child_events[1].has_stream_info());
    EXPECT_EQ_POINTER(child_events[2].pointer_sample(), viewport_to_view_transform, 5.f, 0.f);
    EXPECT_FALSE(child_events[2].has_stream_info());
    EXPECT_EQ_POINTER(child_events[3].pointer_sample(), viewport_to_view_transform, 0.f, 0.f);
    EXPECT_FALSE(child_events[3].has_stream_info());
  }
}

// In this test we set up the context and the target. We apply clip space transform to the camera
// and then inject pointer events to confirm that the coordinates received by the listener are
// not impacted by the clip space transform.
TEST_F(GfxMouseIntegrationTest, ClipSpaceTransformedScene_ShouldHaveNoImpactOnOutput) {
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
  auto [child_session, child_mouse_source] = CreateChildView(
      std::move(v2), std::move(child_control_ref), fidl::Clone(child_view_ref), "child_view");
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation.
  RegisterInjector(std::move(root_view_ref), std::move(child_view_ref));
  Inject(0, 0, EventPhase::ADD);
  Inject(5, 0, EventPhase::CHANGE);
  Inject(5, 5, EventPhase::CHANGE);
  Inject(0, 5, EventPhase::CHANGE);
  RunLoopUntil([&child_events] { return child_events.size() == 4u; });  // Succeeds or times out.

  // Target should receive identical events to injected, since their coordinate spaces are the same.
  {
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    EXPECT_EQ_POINTER(child_events[0].pointer_sample(), viewport_to_view_transform, 0.f, 0.f);
    EXPECT_EQ_POINTER(child_events[1].pointer_sample(), viewport_to_view_transform, 5.f, 0.f);
    EXPECT_EQ_POINTER(child_events[2].pointer_sample(), viewport_to_view_transform, 5.f, 5.f);
    EXPECT_EQ_POINTER(child_events[3].pointer_sample(), viewport_to_view_transform, 0.f, 5.f);
  }
}

// Basic scene (no transformations) where the Viewport is smaller than the Views.
// We then inject two streams: The first has an ADD outside the Viewport, which counts as a miss and
// should not be seen by anyone. The second stream has the ADD inside the Viewport and subsequent
// events outside, and this full stream should be seen by the target.
TEST_F(GfxMouseIntegrationTest, InjectionOutsideViewport_ShouldLimitOnClick) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();
  const uint8_t kButtonId = 1;

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
  auto [child_session, child_mouse_source] = CreateChildView(
      std::move(v2), std::move(child_control_ref), fidl::Clone(child_view_ref), "child_view");
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Scene is now set up, send in the input. The initial click is outside the viewport and
  // the stream should therefore not be seen by anyone.
  const std::vector<uint8_t> button_vec = {kButtonId};
  RegisterInjector(std::move(root_view_ref), std::move(child_view_ref),
                   fuchsia::ui::pointerinjector::DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET,
                   button_vec);
  Inject(10, 10, EventPhase::ADD, button_vec);  // Outside viewport. Button down.
  // Remainder inside viewport, but should not be delivered.
  Inject(5, 0, EventPhase::CHANGE, button_vec);
  Inject(5, 5, EventPhase::CHANGE, button_vec);
  Inject(0, 5, EventPhase::CHANGE);  // Button up. Hover event should be delivered.

  // Send in button down starting in the viewport and moving outside.
  Inject(1, 1, EventPhase::CHANGE, button_vec);  // Inside viewport.
  // Remainder outside viewport, but should still be delivered.
  Inject(50, 0, EventPhase::CHANGE, button_vec);
  Inject(50, 50, EventPhase::CHANGE, button_vec);
  Inject(0, 50, EventPhase::CHANGE, button_vec);
  Inject(1, 1, EventPhase::CHANGE);  // Inside viewport. Button up.
  RunLoopUntil([&child_events] { return child_events.size() >= 6u; });  // Succeeds or times out.
  EXPECT_EQ(child_events.size(), 6u);

  {
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    EXPECT_EQ_POINTER_WITH_BUTTONS(child_events[0].pointer_sample(), viewport_to_view_transform,
                                   0.f, 5.f, std::vector<uint8_t>());
    EXPECT_EQ_POINTER_WITH_BUTTONS(child_events[1].pointer_sample(), viewport_to_view_transform,
                                   1.f, 1.f, button_vec);
    EXPECT_EQ_POINTER_WITH_BUTTONS(child_events[2].pointer_sample(), viewport_to_view_transform,
                                   50.f, 0.f, button_vec);
    EXPECT_EQ_POINTER_WITH_BUTTONS(child_events[3].pointer_sample(), viewport_to_view_transform,
                                   50.f, 50.f, button_vec);
    EXPECT_EQ_POINTER_WITH_BUTTONS(child_events[4].pointer_sample(), viewport_to_view_transform,
                                   0.f, 50.f, button_vec);
    EXPECT_EQ_POINTER_WITH_BUTTONS(child_events[5].pointer_sample(), viewport_to_view_transform,
                                   1.f, 1.f, std::vector<uint8_t>());
  }
}

TEST_F(GfxMouseIntegrationTest, HoverTest) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up a scene with two ViewHolders, one a child of the other. Make the Views bigger than the
  // Viewport.
  static constexpr fuchsia::ui::gfx::ViewProperties k100x100x1 = {
      .bounding_box = {.max = {100, 100, 1}}};
  // Set up a scene with two ViewHolders, one a child of the other.
  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  {
    scenic::View view(root_session_->session, std::move(v1), std::move(root_control_ref),
                      fidl::Clone(root_view_ref), "root_view");
    scenic::ViewHolder holder_1(root_session_->session, std::move(vh1), "holder_1");
    holder_1.SetViewProperties(k100x100x1);
    root_session_->scene.AddChild(holder_1);
    scenic::ViewHolder holder_2(root_session_->session, std::move(vh2), "holder_2");
    holder_2.SetViewProperties(k100x100x1);
    view.AddChild(holder_2);
    BlockingPresent(*root_session_->session);
  }

  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  auto [child_session, child_mouse_source] = CreateChildView(
      std::move(v2), std::move(child_control_ref), fidl::Clone(child_view_ref), "child_view");
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  RegisterInjector(std::move(root_view_ref), std::move(child_view_ref),
                   fuchsia::ui::pointerinjector::DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET);
  // Outside viewport.
  Inject(10, 10, EventPhase::ADD);
  // Inside viewport.
  Inject(5, 0, EventPhase::CHANGE);  // "View entered".
  Inject(5, 5, EventPhase::CHANGE);
  Inject(0, 5, EventPhase::CHANGE);
  // Outside viewport.
  Inject(50, 0, EventPhase::CHANGE);  // "View exited".
  Inject(50, 50, EventPhase::CHANGE);
  Inject(0, 50, EventPhase::CHANGE);
  // Inside viewport.
  Inject(1, 1, EventPhase::CHANGE);  // "View entered".

  RunLoopUntil([&child_events] { return child_events.size() >= 5u; });  // Succeeds or times out.
  EXPECT_EQ(child_events.size(), 5u);

  {
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    {
      const auto& event = child_events[0];
      EXPECT_EQ_POINTER(event.pointer_sample(), viewport_to_view_transform, 5.f, 0.f);
      ASSERT_TRUE(event.has_stream_info());
      EXPECT_EQ(event.stream_info().status, MouseViewStatus::ENTERED);
    }
    {
      const auto& event = child_events[1];
      EXPECT_EQ_POINTER(event.pointer_sample(), viewport_to_view_transform, 5.f, 5.f);
      EXPECT_FALSE(event.has_stream_info());
    }
    {
      const auto& event = child_events[2];
      EXPECT_EQ_POINTER(event.pointer_sample(), viewport_to_view_transform, 0.f, 5.f);
      EXPECT_FALSE(event.has_stream_info());
    }
    {
      const auto& event = child_events[3];
      EXPECT_FALSE(event.has_pointer_sample()) << "Should get no pointer sample on View Exit";
      ASSERT_TRUE(event.has_stream_info());
      EXPECT_EQ(event.stream_info().status, MouseViewStatus::EXITED);
    }
    {
      const auto& event = child_events[4];
      EXPECT_EQ_POINTER(event.pointer_sample(), viewport_to_view_transform, 1.f, 1.f);
      ASSERT_TRUE(event.has_stream_info());
      EXPECT_EQ(event.stream_info().status, MouseViewStatus::ENTERED);
    }
  }
}

TEST_F(GfxMouseIntegrationTest, InjectorDeath_ShouldCauseViewExitedEvent) {
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
  auto [child_session, child_mouse_source] = CreateChildView(
      std::move(v2), std::move(child_control_ref), fidl::Clone(child_view_ref), "child_view");
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  RegisterInjector(fidl::Clone(root_view_ref), fidl::Clone(child_view_ref),
                   fuchsia::ui::pointerinjector::DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET);
  Inject(2.5f, 2.5f, EventPhase::ADD);  // "View entered".

  // Register another injector, killing the old channel.
  RegisterInjector(fidl::Clone(root_view_ref), fidl::Clone(child_view_ref),
                   fuchsia::ui::pointerinjector::DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET);

  RunLoopUntil([&child_events] { return child_events.size() >= 2u; });  // Succeeds or times out.
  EXPECT_EQ(child_events.size(), 2u);

  {
    const auto& event = child_events[0];
    EXPECT_TRUE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::ENTERED);
  }
  {
    const auto& event = child_events[1];
    EXPECT_FALSE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::EXITED);
  }
}

TEST_F(GfxMouseIntegrationTest, REMOVEandCANCEL_ShouldCauseViewExitedEvents) {
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
  auto [child_session, child_mouse_source] = CreateChildView(
      std::move(v2), std::move(child_control_ref), fidl::Clone(child_view_ref), "child_view");
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  RegisterInjector(std::move(root_view_ref), std::move(child_view_ref),
                   fuchsia::ui::pointerinjector::DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET);
  Inject(2.5f, 2.5f, EventPhase::ADD);     // "View entered".
  Inject(2.5f, 2.5f, EventPhase::REMOVE);  // "View exited".

  RunLoopUntil([&child_events] { return child_events.size() >= 2u; });  // Succeeds or times out.
  EXPECT_EQ(child_events.size(), 2u);

  {
    const auto& event = child_events[0];
    EXPECT_TRUE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::ENTERED);
  }
  {
    const auto& event = child_events[1];
    EXPECT_FALSE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::EXITED);
  }

  child_events.clear();
  Inject(2.5f, 2.5f, EventPhase::ADD);     // "View entered".
  Inject(2.5f, 2.5f, EventPhase::CANCEL);  // "View exited".

  RunLoopUntil([&child_events] { return child_events.size() >= 2u; });  // Succeeds or times out.
  EXPECT_EQ(child_events.size(), 2u);

  {
    const auto& event = child_events[0];
    EXPECT_TRUE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::ENTERED);
  }
  {
    const auto& event = child_events[1];
    EXPECT_FALSE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::EXITED);
  }
}

}  // namespace integration_tests
