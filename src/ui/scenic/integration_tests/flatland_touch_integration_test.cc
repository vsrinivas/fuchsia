// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/lifecycle/cpp/fidl.h>
#include <fuchsia/ui/pointer/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_identity.h>
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

// These tests exercise the integration between Flatland and the InputSystem, including the
// View-to-View transform logic between the injection point and the receiver.
// Setup:
// - The test fixture sets up the display + the root session and view.
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
using fuchsia::ui::composition::ChildViewWatcher;
using fuchsia::ui::composition::ContentId;
using fuchsia::ui::composition::Flatland;
using fuchsia::ui::composition::FlatlandDisplay;
using fuchsia::ui::composition::ParentViewportWatcher;
using fuchsia::ui::composition::TransformId;
using fuchsia::ui::composition::ViewBoundProtocols;
using fuchsia::ui::composition::ViewportProperties;
using fuchsia::ui::pointer::EventPhase;
using fuchsia::ui::pointer::TouchEvent;
using fuchsia::ui::pointer::TouchResponse;
using fuchsia::ui::pointer::TouchResponseType;
using fuchsia::ui::views::ViewCreationToken;
using fuchsia::ui::views::ViewportCreationToken;
using fuchsia::ui::views::ViewRef;

namespace {

const std::map<std::string, std::string> LocalServices() {
  return {{"fuchsia.ui.composition.Allocator",
           "fuchsia-pkg://fuchsia.com/flatland_integration_tests#meta/scenic.cmx"},
          {"fuchsia.ui.composition.Flatland",
           "fuchsia-pkg://fuchsia.com/flatland_integration_tests#meta/scenic.cmx"},
          {"fuchsia.ui.composition.FlatlandDisplay",
           "fuchsia-pkg://fuchsia.com/flatland_integration_tests#meta/scenic.cmx"},
          {"fuchsia.ui.pointerinjector.Registry",
           "fuchsia-pkg://fuchsia.com/flatland_integration_tests#meta/scenic.cmx"},
          // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
          {"fuchsia.ui.lifecycle.LifecycleController",
           "fuchsia-pkg://fuchsia.com/flatland_integration_tests#meta/scenic.cmx"},
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
  FX_CHECK(transformed_pointer.z != 0);
  const glm::vec3 homogenized = transformed_pointer / transformed_pointer.z;
  return {homogenized.x, homogenized.y};
}

std::pair<ViewCreationToken, ViewportCreationToken> NewViewCreationTokens() {
  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  {
    const auto status = zx::channel::create(0, &parent_token.value, &child_token.value);
    FX_CHECK(status == ZX_OK);
  }
  return std::make_pair<ViewCreationToken, ViewportCreationToken>(std::move(child_token),
                                                                  std::move(parent_token));
}

}  // namespace

class FlatlandTouchIntegrationTest : public gtest::TestWithEnvironmentFixture {
 protected:
  static constexpr uint32_t kDeviceId = 1111;
  static constexpr uint32_t kPointerId = 2222;
  // clang-format off
  static constexpr std::array<float, 9> kIdentityMatrix = {
    1, 0, 0, // column one
    0, 1, 0, // column two
    0, 0, 1, // column three
  };
  // clang-format on

  void SetUp() override {
    TestWithEnvironmentFixture::SetUp();
    environment_ = CreateNewEnclosingEnvironment("flatland_touch_integration_test_environment",
                                                 CreateServices());
    WaitForEnclosingEnvToStart(environment_.get());

    // Connects to scenic lifecycle controller in order to shutdown scenic at the end of the test.
    // This ensures the correct ordering of shutdown under CFv1: first scenic, then the fake display
    // controller.
    //
    // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
    environment_->ConnectToService<fuchsia::ui::lifecycle::LifecycleController>(
        scenic_lifecycle_controller_.NewRequest());

    environment_->ConnectToService(flatland_display_.NewRequest());
    flatland_display_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
    });
    environment_->ConnectToService(pointerinjector_registry_.NewRequest());
    pointerinjector_registry_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to pointerinjector Registry: " << zx_status_get_string(status);
    });

    // Set up root view.
    environment_->ConnectToService(root_session_.NewRequest());
    root_session_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
    });

    fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
    auto [child_token, parent_token] = NewViewCreationTokens();
    flatland_display_->SetContent(std::move(parent_token), child_view_watcher.NewRequest());

    fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    root_view_ref_ = fidl::Clone(identity.view_ref);
    root_session_->CreateView2(std::move(child_token), std::move(identity), {},
                               parent_viewport_watcher.NewRequest());
    parent_viewport_watcher->GetLayout([this](auto layout_info) {
      ASSERT_TRUE(layout_info.has_logical_size());
      const auto [width, height] = layout_info.logical_size();
      display_width_ = static_cast<float>(width);
      display_height_ = static_cast<float>(height);
    });
    BlockingPresent(root_session_);

    // Wait until we get the display size.
    RunLoopUntil([this] { return display_width_ != 0 && display_height_ != 0; });
  }

  void TearDown() override {
    // Avoid spurious errors since we are about to kill scenic.
    //
    // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
    flatland_display_.set_error_handler(nullptr);
    root_session_.set_error_handler(nullptr);
    pointerinjector_registry_.set_error_handler(nullptr);

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

  void BlockingPresent(fuchsia::ui::composition::FlatlandPtr& flatland) {
    bool presented = false;
    flatland.events().OnFramePresented = [&presented](auto) { presented = true; };
    flatland->Present({});
    RunLoopUntil([&presented] { return presented; });
    flatland.events().OnFramePresented = nullptr;
  }

  void Inject(float x, float y, fupi_EventPhase phase) {
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
    pointerinjector_registry_->Register(
        std::move(config), injector_.NewRequest(),
        [&register_callback_fired] { register_callback_fired = true; });
    injector_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
    });
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

  const uint32_t kDefaultSize = 1;
  bool injector_channel_closed_ = false;
  float display_width_ = 0;
  float display_height_ = 0;

  std::array<std::array<float, 2>, 2> FullScreenExtents() const {
    return {{{0, 0}, {display_width_, display_height_}}};
  }

  fuchsia::ui::composition::FlatlandPtr root_session_;
  fuchsia::ui::views::ViewRef root_view_ref_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;

 private:
  fuchsia::ui::lifecycle::LifecycleControllerSyncPtr scenic_lifecycle_controller_;
  fuchsia::ui::composition::FlatlandDisplayPtr flatland_display_;
  fuchsia::ui::pointerinjector::RegistryPtr pointerinjector_registry_;
  fuchsia::ui::pointerinjector::DevicePtr injector_;

  // Holds watch loops so they stay alive through the duration of the test.
  std::vector<std::function<void(std::vector<TouchEvent>)>> watch_loops_;
};

// This test sets up a scene with no transformations. Injected events should go straight through to
// the child unchanged.
TEST_F(FlatlandTouchIntegrationTest, BasicInputTest) {
  fuchsia::ui::composition::FlatlandPtr child_session;
  fuchsia::ui::pointer::TouchSourcePtr child_touch_source;
  environment_->ConnectToService(child_session.NewRequest());
  child_touch_source.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Touch source closed with status: " << zx_status_get_string(status);
  });

  // Set up the root graph.
  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  auto [child_token, parent_token] = NewViewCreationTokens();
  ViewportProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  const TransformId kRootTransform{.value = 1};
  const ContentId kRootContent{.value = 1};
  root_session_->CreateTransform(kRootTransform);
  root_session_->CreateViewport(kRootContent, std::move(parent_token), std::move(properties),
                                child_view_watcher.NewRequest());
  root_session_->SetRootTransform(kRootTransform);
  root_session_->SetContent(kRootTransform, kRootContent);
  BlockingPresent(root_session_);

  // Set up the child view and its TouchSource channel.
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  auto identity = scenic::NewViewIdentityOnCreation();
  auto child_view_ref = fidl::Clone(identity.view_ref);
  fuchsia::ui::composition::ViewBoundProtocols protocols;
  protocols.set_touch_source(child_touch_source.NewRequest());
  child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                             parent_viewport_watcher.NewRequest());
  BlockingPresent(child_session);

  // Listen for input events.
  std::vector<TouchEvent> child_events;
  StartWatchLoop(child_touch_source, child_events);

  // Scene is now set up, send in the input. One event for each corner of the view.
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET);
  Inject(0, 0, fupi_EventPhase::ADD);
  Inject(display_width_, 0, fupi_EventPhase::CHANGE);
  Inject(display_width_, display_height_, fupi_EventPhase::CHANGE);
  Inject(0, display_height_, fupi_EventPhase::REMOVE);
  RunLoopUntil([&child_events] { return child_events.size() == 4u; });  // Succeeds or times out.

  // Target should receive identical events to injected, since their coordinate spaces are the same.
  {
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    EXPECT_EQ_POINTER(child_events[0].pointer_sample(), viewport_to_view_transform, EventPhase::ADD,
                      0.f, 0.f);
    EXPECT_EQ_POINTER(child_events[1].pointer_sample(), viewport_to_view_transform,
                      EventPhase::CHANGE, display_width_, 0.f);
    EXPECT_EQ_POINTER(child_events[2].pointer_sample(), viewport_to_view_transform,
                      EventPhase::CHANGE, display_width_, display_height_);
    EXPECT_EQ_POINTER(child_events[3].pointer_sample(), viewport_to_view_transform,
                      EventPhase::REMOVE, 0.f, display_height_);
  }
}

}  // namespace integration_tests
