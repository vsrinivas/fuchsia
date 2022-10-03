// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/input/injection/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/test/input/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/status.h>
#include <zircon/types.h>
#include <zircon/utc.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <test/accessibility/cpp/fidl.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/testing/ui_test_manager/ui_test_manager.h"

// This test exercises the pointer injector code in the context of Input Pipeline and a real Scenic
// client. It is a multi-component test, and carefully avoids sleeping or polling for component
// coordination.
// - It runs real (Root Presenter + Input Pipeline | Scene Manager) components, and a real Scenic
// component.
// - It uses a fake display controller; the physical device is unused.
//
// Components involved
// - This test program
// - Root Presenter (with separate Input Pipeline) or Scene Manager
// - Scenic
// - Child view, a Scenic client
//
// Touch dispatch path
// - Test program's injection -> Input Pipeline -> Scenic -> Child view
//
// Setup sequence
// - The test sets up this view hierarchy:
//   - Top level scene, owned by Root Presenter.
//   - Child view, owned by the ui client.
// - The test waits for a Scenic event that verifies the child has UI content in the scene graph.
// - The test injects input into Input Pipeline, emulating a display's touch report.
// - Input Pipeline dispatches the touch event to Scenic, which in turn dispatches it to the child.
// - The child receives the touch event and reports back to the test over a custom test-only FIDL.
// - Test waits for the child to report a touch; when the test receives the report, the test quits
//   successfully.
//
// This test uses the realm_builder library to construct the topology of components
// and routes services between them. For v2 components, every test driver component
// sits as a child of test_manager in the topology. Thus, the topology of a test
// driver component such as this one looks like this:
//
//     test_manager
//         |
//   pointerinjector-config-test-ip.cml (this component)
//
// With the usage of the realm_builder library, we construct a realm during runtime
// and then extend the topology to look like:
//
//    test_manager
//         |
//   pointerinjector-config-test-ip.cml (this component)
//         |
//   <created realm root>
//      /      \
//   scenic  input-pipeline
//
// For more information about testing v2 components and realm_builder,
// visit the following links:
//
// Testing: https://fuchsia.dev/fuchsia-src/concepts/testing/v2
// Realm Builder: https://fuchsia.dev/fuchsia-src/development/components/v2/realm_builder

namespace {

using ScenicEvent = fuchsia::ui::scenic::Event;
using GfxEvent = fuchsia::ui::gfx::Event;

// Types imported for the realm_builder library.
using component_testing::ChildRef;
using component_testing::LocalComponent;
using component_testing::LocalComponentHandles;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Realm;
using component_testing::Route;
using RealmBuilder = component_testing::RealmBuilder;

// Alias for Component child name as provided to Realm Builder.
using ChildName = std::string;

// Alias for Component Legacy URL as provided to Realm Builder.
using LegacyUrl = std::string;

// Max timeout in failure cases.
// Set this as low as you can that still works across all test platforms.
constexpr zx::duration kTimeout = zx::min(5);

// Maximum distance between two view coordinates so that they are considered equal.
constexpr auto kViewCoordinateEpsilon = 0.01;

constexpr auto kMockResponseListener = "response_listener";

constexpr auto kTapRetryInterval = zx::sec(1);

enum class TapLocation { kTopLeft };

// This component implements fuchsia.ui.test.input.TouchInputListener
// and the interface for a RealmBuilder LocalComponent. A LocalComponent
// is a component that is implemented here in the test, as opposed to elsewhere
// in the system. When it's inserted to the realm, it will act like a proper
// component. This is accomplished, in part, because the realm_builder
// library creates the necessary plumbing. It creates a manifest for the component
// and routes all capabilities to and from it.
class ResponseListenerServer : public fuchsia::ui::test::input::TouchInputListener,
                               public LocalComponent {
 public:
  explicit ResponseListenerServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  // |fuchsia::ui::test::input::TouchInputListener|
  void ReportTouchInput(
      fuchsia::ui::test::input::TouchInputListenerReportTouchInputRequest request) override {
    FX_CHECK(respond_callback_) << "Expected callback to be set.";
    respond_callback_(std::move(request));
  }

  // |LocalComponent::Start|
  // When the component framework requests for this component to start, this
  // method will be invoked by the realm_builder library.
  void Start(std::unique_ptr<LocalComponentHandles> local_handles) override {
    // When this component starts, add a binding to the test.touch.ResponseListener
    // protocol to this component's outgoing directory.
    FX_CHECK(local_handles->outgoing()->AddPublicService(
                 fidl::InterfaceRequestHandler<fuchsia::ui::test::input::TouchInputListener>(
                     [this](auto request) {
                       bindings_.AddBinding(this, std::move(request), dispatcher_);
                     })) == ZX_OK);
    local_handles_.emplace_back(std::move(local_handles));
  }

  void SetRespondCallback(
      fit::function<void(fuchsia::ui::test::input::TouchInputListenerReportTouchInputRequest)>
          callback) {
    respond_callback_ = std::move(callback);
  }

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  std::vector<std::unique_ptr<LocalComponentHandles>> local_handles_;
  fidl::BindingSet<fuchsia::ui::test::input::TouchInputListener> bindings_;
  fit::function<void(fuchsia::ui::test::input::TouchInputListenerReportTouchInputRequest)>
      respond_callback_;
};

struct PointerInjectorConfigTestData {
  int display_rotation;

  float clip_scale = 1.f;
  float clip_translation_x = 0.f;
  float clip_translation_y = 0.f;

  // expected location of the pointer event, in client view space, where the
  // range of the X and Y axes is [0, 1]
  float expected_x;
  float expected_y;
};

using PointerInjectorConfigTestParams =
    std::tuple<ui_testing::UITestRealm::SceneOwnerType, PointerInjectorConfigTestData>;

class PointerInjectorConfigTest
    : public gtest::RealLoopFixture,
      public testing::WithParamInterface<PointerInjectorConfigTestParams> {
 protected:
  PointerInjectorConfigTest() = default;
  ~PointerInjectorConfigTest() override {
    FX_CHECK(injection_count_ > 0) << "injection expected but didn't happen.";
  }

  void SetUp() override {
    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTimeout);

    auto [scene_owner, test_data] = GetParam();

    ui_testing::UITestRealm::Config config;
    config.display_rotation = test_data.display_rotation;
    config.scene_owner = scene_owner;
    config.use_input = true;
    config.accessibility_owner = ui_testing::UITestRealm::AccessibilityOwnerType::FAKE;
    config.ui_to_client_services = {fuchsia::ui::scenic::Scenic::Name_};
    ui_test_manager_ = std::make_unique<ui_testing::UITestManager>(std::move(config));

    // Assemble realm.
    BuildRealm();

    // Get the display dimensions.
    FX_LOGS(INFO) << "Waiting for scenic display info";

    auto [width, height] = ui_test_manager_->GetDisplayDimensions();
    display_width_ = static_cast<uint32_t>(width);
    display_height_ = static_cast<uint32_t>(height);
    FX_LOGS(INFO) << "Got display_width = " << display_width_
                  << " and display_height = " << display_height_;

    // Register input injection device.
    FX_LOGS(INFO) << "Registering input injection device";
    RegisterInjectionDevice();

    // Launch client view, and wait until it's rendering to proceed with the test.
    ui_test_manager_->InitializeScene();
    RunLoopUntil([this]() { return ui_test_manager_->ClientViewIsRendering(); });

    realm_exposed_services_->Connect<test::accessibility::Magnifier>(
        this->fake_magnifier_.NewRequest());
  }

  // Waits for one or more pointer events; calls QuitLoop once one meets expectations.
  void WaitForAResponseMeetingExpectations(float expected_x, float expected_y,
                                           const std::string& component_name) {
    response_listener()->SetRespondCallback(
        [this, expected_x, expected_y, component_name](
            fuchsia::ui::test::input::TouchInputListenerReportTouchInputRequest request) {
          FX_LOGS(INFO) << "Client received tap at (" << request.local_x() << ", "
                        << request.local_y() << ").";
          FX_LOGS(INFO) << "Expected tap is at approximately (" << expected_x << ", " << expected_y
                        << ").";

          // Allow for minor rounding differences in coordinates.
          EXPECT_EQ(request.component_name(), component_name);
          if (abs(request.local_x() - expected_x) <= kViewCoordinateEpsilon &&
              abs(request.local_y() - expected_y) <= kViewCoordinateEpsilon) {
            response_listener()->SetRespondCallback([](auto) {});
            QuitLoop();
          }
        });
  }

  void RegisterInjectionDevice() {
    FX_LOGS(INFO) << "Registering fake touch screen";
    input_registry_ =
        realm_exposed_services()->template Connect<fuchsia::ui::test::input::Registry>();
    input_registry_.set_error_handler([](auto) { FX_LOGS(ERROR) << "Error from input helper"; });

    bool touchscreen_registered = false;
    fuchsia::ui::test::input::RegistryRegisterTouchScreenRequest request;
    request.set_device(fake_touchscreen_.NewRequest());
    input_registry_->RegisterTouchScreen(
        std::move(request), [&touchscreen_registered]() { touchscreen_registered = true; });

    RunLoopUntil([&touchscreen_registered] { return touchscreen_registered; });
    FX_LOGS(INFO) << "Touchscreen registered";
  }

  void TapTopLeft() {
    fuchsia::ui::test::input::TouchScreenSimulateTapRequest tap_request;

    auto [scene_owner, test_data] = GetParam();

    // Inject one input report, then a conclusion (empty) report.
    switch (test_data.display_rotation) {
      case 0:
        tap_request.mutable_tap_location()->x = -500;
        tap_request.mutable_tap_location()->y = -500;
        break;
      case 90:
        // The /config/data/display_rotation (90) specifies how many degrees to rotate the
        // presentation child view, counter-clockwise, in a right-handed coordinate system. Thus,
        // the user observes the child view to rotate *clockwise* by that amount (90).
        tap_request.mutable_tap_location()->x = 500;
        tap_request.mutable_tap_location()->y = -500;
        break;
      default:
        FX_NOTREACHED();
    }

    FX_LOGS(INFO) << "Injecting tap at (" << tap_request.tap_location().x << ", "
                  << tap_request.tap_location().y << ")";
    fake_touchscreen_->SimulateTap(std::move(tap_request), [this]() {
      ++injection_count_;
      FX_LOGS(INFO) << "*** Tap injected, count: " << injection_count_;
    });
  }

  // Try injecting a tap every `kTapRetryInterval` until the test completes.
  void TryInjectRepeatedly(TapLocation tap_location) {
    TapTopLeft();
    async::PostDelayedTask(
        dispatcher(), [this, tap_location] { TryInjectRepeatedly(tap_location); },
        kTapRetryInterval);
  }

  void SetClipSpaceTransform(float scale, float x, float y) {
    fake_magnifier_->SetMagnification(scale, x, y);
  }

  // Guaranteed to be initialized after SetUp().
  float display_width() const { return static_cast<float>(display_width_); }
  float display_height() const { return static_cast<float>(display_height_); }

  sys::ServiceDirectory* realm_exposed_services() { return realm_exposed_services_.get(); }
  Realm* realm() { return realm_.get(); }

  ResponseListenerServer* response_listener() { return response_listener_.get(); }

 private:
  void BuildRealm() {
    FX_LOGS(INFO) << "Building realm";
    realm_ = std::make_unique<Realm>(ui_test_manager_->AddSubrealm());

    // Key part of service setup: have this test component vend the
    // |ResponseListener| service in the constructed realm.
    response_listener_ = std::make_unique<ResponseListenerServer>(dispatcher());
    realm()->AddLocalChild(kMockResponseListener, response_listener_.get());

    realm()->AddChild(kCppGfxClient, kCppGfxClientUrl);

    realm()->AddRoute({.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                       .source = ChildRef{kCppGfxClient},
                       .targets = {ParentRef()}});
    realm()->AddRoute(
        {.capabilities = {Protocol{fuchsia::ui::test::input::TouchInputListener::Name_}},
         .source = ChildRef{kMockResponseListener},
         .targets = {ChildRef{kCppGfxClient}}});
    realm()->AddRoute({.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                       .source = ParentRef(),
                       .targets = {ChildRef{kCppGfxClient}}});

    ui_test_manager_->BuildRealm();

    realm_exposed_services_ = ui_test_manager_->CloneExposedServicesDirectory();
  }

  std::unique_ptr<ui_testing::UITestManager> ui_test_manager_;
  std::unique_ptr<sys::ServiceDirectory> realm_exposed_services_;
  std::unique_ptr<Realm> realm_;

  std::unique_ptr<ResponseListenerServer> response_listener_;

  fuchsia::ui::test::input::RegistryPtr input_registry_;
  fuchsia::ui::test::input::TouchScreenPtr fake_touchscreen_;

  int injection_count_ = 0;

  uint32_t display_width_ = 0;
  uint32_t display_height_ = 0;

  test::accessibility::MagnifierSyncPtr fake_magnifier_;

  static constexpr auto kCppGfxClient = "gfx_client";
  static constexpr auto kCppGfxClientUrl = "#meta/touch-gfx-client.cm";
};

// Declare test data.
// In all these tests, we tap the center of the top left quadrant of the
// physical display (after rotation), and verify that the client view gets a
// pointer event with the expected coordinates.

// No changes to display rotation or clip space
constexpr PointerInjectorConfigTestData kTestDataBaseCase = {
    .display_rotation = 0, .expected_x = 1.f / 4.f, .expected_y = 1.f / 4.f};

// Test scale by a factor of 2.
// Intuitive argument for these expected coordinates:
// Here we've zoomed into the center of the client view, scaling it up by 2x. So, the touch point
// will have 'migrated' halfway towards the center of the client view:  3/8 instead of 1/4.
constexpr PointerInjectorConfigTestData kTestDataScale = {
    .display_rotation = 0, .clip_scale = 2.f, .expected_x = 3.f / 8.f, .expected_y = 3.f / 8.f};

// Test display rotation by 90 degrees.
// In this case, rotation shouldn't affect what the client view sees.
constexpr PointerInjectorConfigTestData kTestDataRotateAndScale = {
    .display_rotation = 90, .clip_scale = 2.f, .expected_x = 3.f / 8.f, .expected_y = 3.f / 8.f};

// Test scaling and translation.
constexpr float kScale = 3.f;
constexpr float kTranslationX = -0.2f;
constexpr float kTranslationY = 0.1f;
constexpr PointerInjectorConfigTestData kTestDataScaleAndTranslate = {
    .display_rotation = 0,
    .clip_scale = kScale,
    .clip_translation_x = kTranslationX,
    .clip_translation_y = kTranslationY,
    // Terms: 'Original position' + 'movement due to scale' + 'movement due to translation'
    .expected_x = 0.25f + 0.25f * (1.f - 1.f / kScale) - kTranslationX / 2.f / kScale,
    .expected_y = 0.25f + 0.25f * (1.f - 1.f / kScale) - kTranslationY / 2.f / kScale};

// Test scaling, translation, and rotation at once.
//
// Here, the translation does affect what the client view sees, so we have to account for it.
// This is what the translation looks like in client view coordinates, where it's rotated 90
// degrees.
constexpr float kClientViewTranslationX = kTranslationY;
constexpr float kClientViewTranslationY = -kTranslationX;
constexpr PointerInjectorConfigTestData kTestDataScaleTranslateRotate = {
    .display_rotation = 90,
    .clip_scale = kScale,
    .clip_translation_x = kTranslationX,
    .clip_translation_y = kTranslationY,
    // Same formula as before, but with different transform values.
    .expected_x = 0.25f + 0.25f * (1.f - 1.f / kScale) - kClientViewTranslationX / 2.f / kScale,
    .expected_y = 0.25f + 0.25f * (1.f - 1.f / kScale) - kClientViewTranslationY / 2.f / kScale};

INSTANTIATE_TEST_SUITE_P(
    PointerInjectorConfigTestWithParams, PointerInjectorConfigTest,
    ::testing::Combine(::testing::Values(ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER,
                                         ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER),
                       ::testing::Values(kTestDataBaseCase, kTestDataScale, kTestDataRotateAndScale,
                                         kTestDataScaleAndTranslate,
                                         kTestDataScaleTranslateRotate)));

TEST_P(PointerInjectorConfigTest, CppGfxClientTapTest) {
  auto [scene_owner, test_data] = GetParam();

  FX_LOGS(INFO) << "Starting test with params: display_rotation=" << test_data.display_rotation
                << ", clip_scale=" << test_data.clip_scale
                << ", clip_translation_x=" << test_data.clip_translation_x
                << ", clip_translation_y=" << test_data.clip_translation_y
                << ", expected_x=" << test_data.expected_x
                << ", expected_y=" << test_data.expected_y;

  SetClipSpaceTransform(test_data.clip_scale, test_data.clip_translation_x,
                        test_data.clip_translation_y);

  TryInjectRepeatedly(TapLocation::kTopLeft);

  switch (test_data.display_rotation) {
    case 0:
      WaitForAResponseMeetingExpectations(
          /*expected_x=*/display_width() * test_data.expected_x,
          /*expected_y=*/display_height() * test_data.expected_y,
          /*component_name=*/"touch-gfx-client");
      break;
    case 90:
      WaitForAResponseMeetingExpectations(
          /*expected_x=*/display_height() * test_data.expected_x,
          /*expected_y=*/display_width() * test_data.expected_y,
          /*component_name=*/"touch-gfx-client");
      break;
    default:
      FX_NOTREACHED();
  }

  RunLoop();
}

}  // namespace
