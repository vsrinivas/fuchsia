// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include <gtest/gtest.h>
#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/strings/join_strings.h>
#include <src/lib/testing/loop_fixture/test_loop_fixture.h>

#include "src/ui/a11y/lib/view/a11y_view.h"
#include "src/ui/bin/root_presenter/app.h"
#include "src/ui/bin/root_presenter/presentation.h"
#include "src/ui/bin/root_presenter/tests/fakes/fake_keyboard_focus_controller.h"
#include "src/ui/bin/root_presenter/tests/fakes/fake_view.h"
#include "src/ui/input/lib/injector/tests/mocks/mock_injector_registry.h"

namespace root_presenter {
namespace {

zx_koid_t ExtractKoid(const fuchsia::ui::views::ViewRef& view_ref) {
  zx_info_handle_basic_t info{};
  if (view_ref.reference.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) !=
      ZX_OK) {
    return ZX_KOID_INVALID;  // no info
  }

  return info.koid;
}

class RootPresenterTest : public gtest::RealLoopFixture,
                          public fuchsia::ui::focus::FocusChainListener {
 public:
  RootPresenterTest() : focus_listener_(this) {}

  void SetUp() final {
    real_component_context_ = sys::ComponentContext::CreateAndServeOutgoingDirectory();

    // Proxy real APIs through the fake component_context.
    // TODO(fxbug.dev/74262): The test should set up a test environment instead of
    // injecting a real scenic in the sandbox.
    ASSERT_EQ(
        ZX_OK,
        context_provider_.service_directory_provider()->AddService<fuchsia::ui::scenic::Scenic>(
            [this](fidl::InterfaceRequest<fuchsia::ui::scenic::Scenic> request) {
              real_component_context_->svc()->Connect(std::move(request));
            }));
    // Connect FocusChainListenerRegistry to the real Scenic injected in the test sandbox.
    ASSERT_EQ(ZX_OK,
              context_provider_.service_directory_provider()
                  ->AddService<fuchsia::ui::focus::FocusChainListenerRegistry>(
                      [this](fidl::InterfaceRequest<fuchsia::ui::focus::FocusChainListenerRegistry>
                                 request) {
                        real_component_context_->svc()->Connect(std::move(request));
                      }));

    keyboard_focus_ctl_ = std::make_unique<testing::FakeKeyboardFocusController>(context_provider_);

    // Start RootPresenter with fake context.
    root_presenter_ = std::make_unique<App>(context_provider_.context(), [this] { QuitLoop(); });
  }

  void TearDown() final { root_presenter_.reset(); }

  App* root_presenter() { return root_presenter_.get(); }
  Presentation* presentation() { return root_presenter_->presentation(); }

  void ConnectInjectorRegistry(bool use_fake = true) {
    if (use_fake) {
      injector_registry_ = std::make_unique<input::test::MockInjectorRegistry>(context_provider_);
    } else {
      ASSERT_EQ(
          ZX_OK,
          context_provider_.service_directory_provider()
              ->AddService<fuchsia::ui::pointerinjector::Registry>(
                  [this](fidl::InterfaceRequest<fuchsia::ui::pointerinjector::Registry> request) {
                    real_component_context_->svc()->Connect(std::move(request));
                  }));
    }

    context_provider_.ConnectToPublicService<fuchsia::ui::input::InputDeviceRegistry>(
        input_device_registry_ptr_.NewRequest());
    input_device_registry_ptr_.set_error_handler([](auto...) { FAIL(); });
  }

  // The a11y view attempts to connect via the context's svc directory. Since
  // root presenter serves the accessibility view registry to its public service
  // directory, we need to re-route the service through the svc directory.
  void ConnectAccessibilityViewRegistry() {
    ASSERT_EQ(
        ZX_OK,
        context_provider_.service_directory_provider()
            ->AddService<fuchsia::ui::accessibility::view::Registry>(
                [this](fidl::InterfaceRequest<fuchsia::ui::accessibility::view::Registry> request) {
                  context_provider_.public_service_directory()->Connect(std::move(request));
                }));
  }

  void SetUpInputTest(bool use_mock_injector_registry = true) {
    ConnectInjectorRegistry(use_mock_injector_registry);

    // Present a fake view.
    fuchsia::ui::scenic::ScenicPtr scenic =
        context_provider_.context()->svc()->Connect<fuchsia::ui::scenic::Scenic>();
    testing::FakeView fake_view(context_provider_.context(), std::move(scenic));
    presentation()->PresentView(fake_view.view_holder_token(), nullptr);

    RunLoopUntil([this]() {
      return presentation()->is_initialized() && presentation()->ready_for_injection();
    });
  }

  void SetUpFocusChainListener(
      fit::function<void(fuchsia::ui::focus::FocusChain focus_chain)> callback) {
    focus_callback_ = std::move(callback);

    fuchsia::ui::focus::FocusChainListenerRegistryPtr focus_chain_listener_registry;
    real_component_context_->svc()->Connect(focus_chain_listener_registry.NewRequest());
    focus_chain_listener_registry.set_error_handler([](zx_status_t status) {
      FX_LOGS(ERROR) << "FocusChainListenerRegistry connection failed with status: "
                     << zx_status_get_string(status);
      FAIL();
    });
    focus_chain_listener_registry->Register(focus_listener_.NewBinding());

    RunLoopUntil([this] { return focus_set_up_; });
  }

  // |fuchsia.ui.focus.FocusChainListener|
  void OnFocusChange(fuchsia::ui::focus::FocusChain focus_chain,
                     OnFocusChangeCallback callback) override {
    focus_set_up_ = true;
    focus_callback_(std::move(focus_chain));
    callback();
  }

  fuchsia::ui::input::DeviceDescriptor TouchscreenDescriptorTemplate() {
    fuchsia::ui::input::DeviceDescriptor descriptor;
    {
      descriptor.touchscreen = std::make_unique<fuchsia::ui::input::TouchscreenDescriptor>();
      {
        fuchsia::ui::input::Axis x_axis, y_axis;
        x_axis.range = fuchsia::ui::input::Range{.min = 0, .max = 10};
        y_axis.range = fuchsia::ui::input::Range{.min = 0, .max = 10};
        descriptor.touchscreen->x = std::move(x_axis);
        descriptor.touchscreen->y = std::move(y_axis);
      }
      descriptor.touchscreen->max_finger_id = 10;
    }
    return descriptor;
  }

  fuchsia::ui::input::InputReport TouchscreenReportTemplate() {
    fuchsia::ui::input::InputReport input_report{
        .touchscreen = std::make_unique<fuchsia::ui::input::TouchscreenReport>()};
    input_report.touchscreen->touches.push_back(
        {.finger_id = 1, .x = 5, .y = 5, .width = 1, .height = 1});
    return input_report;
  }

  fuchsia::ui::input::DeviceDescriptor MediaButtonsDescriptorTemplate() {
    fuchsia::ui::input::DeviceDescriptor descriptor;
    {
      descriptor.media_buttons = std::make_unique<fuchsia::ui::input::MediaButtonsDescriptor>();
      descriptor.media_buttons->buttons =
          fuchsia::ui::input::kVolumeUp | fuchsia::ui::input::kVolumeDown;
    }
    return descriptor;
  }

  fuchsia::ui::input::InputReport MediaButtonsReportTemplate() {
    fuchsia::ui::input::InputReport input_report{
        .media_buttons = std::make_unique<fuchsia::ui::input::MediaButtonsReport>()};
    input_report.media_buttons->volume_up = true;
    return input_report;
  }

  std::vector<inspect::UintArrayValue::HistogramBucket> GetHistogramBuckets(
      const std::vector<std::string>& path, const std::string& property) {
    inspect::Hierarchy root =
        inspect::ReadFromVmo(root_presenter()->inspector()->CopyVmo()).take_value();

    const inspect::Hierarchy* parent = root.GetByPath(path);
    FX_CHECK(parent) << "no node found at path " << fxl::JoinStrings(path, "/");
    const inspect::UintArrayValue* histogram =
        parent->node().get_property<inspect::UintArrayValue>(property);
    FX_CHECK(histogram) << "no histogram named " << property << " in node with path "
                        << fxl::JoinStrings(path, "/");
    return histogram->GetBuckets();
  }

  std::unique_ptr<input::test::MockInjectorRegistry> injector_registry_;
  std::unique_ptr<testing::FakeKeyboardFocusController> keyboard_focus_ctl_;
  fuchsia::ui::input::InputDeviceRegistryPtr input_device_registry_ptr_;
  sys::testing::ComponentContextProvider context_provider_;

 private:
  std::unique_ptr<sys::ComponentContext> real_component_context_;
  std::unique_ptr<App> root_presenter_;

  fidl::Binding<fuchsia::ui::focus::FocusChainListener> focus_listener_;
  fit::function<void(fuchsia::ui::focus::FocusChain focus_chain)> focus_callback_;
  bool focus_set_up_ = false;

  fuchsia::ui::views::ViewToken view_token_;
};

TEST_F(RootPresenterTest, TestSceneSetup) {
  // Present a fake view.
  fuchsia::ui::scenic::ScenicPtr scenic =
      context_provider_.context()->svc()->Connect<fuchsia::ui::scenic::Scenic>();
  testing::FakeView fake_view(context_provider_.context(), std::move(scenic));
  presentation()->PresentView(fake_view.view_holder_token(), nullptr);

  // Run until the view is attached to the scene.
  RunLoopUntil([&fake_view]() { return fake_view.IsAttachedToScene(); });
}

TEST_F(RootPresenterTest, TestAttachA11yView) {
  ConnectInjectorRegistry(/* use_fake = */ false);
  ConnectAccessibilityViewRegistry();
  RunLoopUntilIdle();

  // Present a fake view.
  fuchsia::ui::scenic::ScenicPtr scenic =
      context_provider_.context()->svc()->Connect<fuchsia::ui::scenic::Scenic>();
  testing::FakeView fake_view(context_provider_.context(), std::move(scenic));
  presentation()->PresentView(fake_view.view_holder_token(), nullptr);

  // Run until the view is attached to the scene.
  RunLoopUntil([&fake_view]() { return fake_view.IsAttachedToScene(); });

  // Add an a11y view.
  a11y::AccessibilityView a11y_view(context_provider_.context());

  // Verify that nothing crashes during a11y view setup.
  RunLoopUntil([&a11y_view]() { return a11y_view.is_initialized(); });

  // Add a rectangle to the fakeview so that hit testing will return a result.
  auto view_properties = a11y_view.get_a11y_view_properties();
  auto x = view_properties->bounding_box.min.x;
  auto y = view_properties->bounding_box.min.y;
  auto width = view_properties->bounding_box.max.x - view_properties->bounding_box.min.x;
  auto height = view_properties->bounding_box.max.y - view_properties->bounding_box.min.y;
  bool rectangle_added = false;
  fake_view.AddRectangle(width, height, x, y, rectangle_added);
  RunLoopUntil([&rectangle_added] { return rectangle_added; });

  fake_view.clear_events();

  // Register an input device.
  fuchsia::ui::input::InputDevicePtr input_device_ptr;
  bool channel_error = false;
  input_device_ptr.set_error_handler([&channel_error](auto...) { channel_error = true; });
  input_device_registry_ptr_->RegisterDevice(TouchscreenDescriptorTemplate(),
                                             input_device_ptr.NewRequest());

  RunLoopUntilIdle();

  // Inject a touch event.
  input_device_ptr->DispatchReport(TouchscreenReportTemplate());

  // Verify that client view receives the event.
  RunLoopUntil([&fake_view]() {
    const auto& view_events = fake_view.events();
    for (const auto& event : view_events) {
      // We're looking for the view attached event, so skip any events that are
      // not gfx events.
      if (event.Which() == fuchsia::ui::scenic::Event::Tag::kInput) {
        return true;
      }
    }

    return false;
  });
}

TEST_F(RootPresenterTest, TestAttachA11yViewBeforeClient) {
  ConnectInjectorRegistry(/* use_fake = */ true);
  ConnectAccessibilityViewRegistry();
  RunLoopUntilIdle();

  a11y::AccessibilityView a11y_view(context_provider_.context());

  RunLoopUntilIdle();

  // The a11y view should wait to complete its setup until the client view is
  // attached.
  EXPECT_FALSE(a11y_view.is_initialized());

  // Present a fake view.
  auto scenic = context_provider_.context()->svc()->Connect<fuchsia::ui::scenic::Scenic>();
  testing::FakeView fake_view(context_provider_.context(), std::move(scenic));
  presentation()->PresentView(fake_view.view_holder_token(), nullptr);

  // Run until the view is attached to the scene.
  RunLoopUntil([&fake_view]() { return fake_view.IsAttachedToScene(); });

  // Run loop until a11y view is attached to the scene.
  RunLoopUntil([&a11y_view]() { return a11y_view.is_initialized(); });
}

TEST_F(RootPresenterTest, SinglePresentView_ShouldSucceed) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  fidl::InterfacePtr<fuchsia::ui::policy::Presentation> presentation_ptr;
  bool alive = true;
  presentation_ptr.set_error_handler([&alive](auto) { alive = false; });
  presentation()->PresentView(std::move(view_holder_token), presentation_ptr.NewRequest());

  RunLoopUntilIdle();

  EXPECT_TRUE(alive);
}

TEST_F(RootPresenterTest, SecondPresentView_ShouldFail_AndOriginalShouldSurvive) {
  auto [view_token1, view_holder_token1] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();

  fidl::InterfacePtr<fuchsia::ui::policy::Presentation> presentation1;
  bool alive1 = true;
  presentation1.set_error_handler([&alive1](auto) { alive1 = false; });
  presentation()->PresentView(std::move(view_holder_token1), presentation1.NewRequest());

  fidl::InterfacePtr<fuchsia::ui::policy::Presentation> presentation2;
  bool alive2 = true;
  zx_status_t error = ZX_OK;
  presentation2.set_error_handler([&alive2, &error](zx_status_t err) {
    alive2 = false;
    error = err;
  });
  presentation()->PresentView(std::move(view_holder_token2), presentation2.NewRequest());

  RunLoopUntilIdle();

  EXPECT_TRUE(alive1);
  EXPECT_FALSE(alive2);
  EXPECT_EQ(error, ZX_ERR_ALREADY_BOUND)
      << "Should be: " << zx_status_get_string(ZX_ERR_ALREADY_BOUND)
      << " Was: " << zx_status_get_string(error);
}

TEST_F(RootPresenterTest, SinglePresentOrReplaceView_ShouldSucceeed) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  fidl::InterfacePtr<fuchsia::ui::policy::Presentation> presentation_ptr;
  bool alive = true;
  presentation_ptr.set_error_handler([&alive](auto) { alive = false; });
  presentation()->PresentView(std::move(view_holder_token), presentation_ptr.NewRequest());

  RunLoopUntilIdle();

  EXPECT_TRUE(alive);
}

TEST_F(RootPresenterTest, SecondPresentOrReplaceView_ShouldSucceeed_AndOriginalShouldDie) {
  auto [view_token1, view_holder_token1] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();

  fidl::InterfacePtr<fuchsia::ui::policy::Presentation> presentation1;
  bool alive1 = true;
  zx_status_t error = ZX_OK;
  presentation1.set_error_handler([&alive1, &error](zx_status_t err) {
    alive1 = false;
    error = err;
  });
  presentation()->PresentOrReplaceView(std::move(view_holder_token1), presentation1.NewRequest());

  fidl::InterfacePtr<fuchsia::ui::policy::Presentation> presentation2;
  bool alive2 = true;
  presentation2.set_error_handler([&alive2](auto) { alive2 = false; });
  presentation()->PresentOrReplaceView(std::move(view_holder_token2), presentation2.NewRequest());

  RunLoopUntilIdle();

  EXPECT_FALSE(alive1);
  EXPECT_EQ(error, ZX_ERR_PEER_CLOSED) << "Should be: " << zx_status_get_string(ZX_ERR_PEER_CLOSED)
                                       << " Was: " << zx_status_get_string(error);
  EXPECT_TRUE(alive2);
}

TEST_F(RootPresenterTest, InputInjectionRegistration) {
  SetUpInputTest();

  EXPECT_EQ(injector_registry_->num_registered(), 0u);

  fuchsia::ui::input::InputDevicePtr input_device_ptr;
  bool channel_error = false;
  input_device_ptr.set_error_handler([&channel_error](auto...) { channel_error = true; });
  input_device_registry_ptr_->RegisterDevice(TouchscreenDescriptorTemplate(),
                                             input_device_ptr.NewRequest());
  input_device_ptr->DispatchReport(TouchscreenReportTemplate());
  RunLoopUntilIdle();

  // After the first event a connection to the registry should have been made.
  EXPECT_EQ(injector_registry_->num_registered(), 1u);
  EXPECT_EQ(injector_registry_->num_events_received(), 1u);
  EXPECT_FALSE(channel_error);

  // After all events have been handled and the channel's been closed, the channel to the registry
  // should also close.
  input_device_ptr.Unbind();
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry_->num_registered(), 0u);
  EXPECT_FALSE(channel_error);
}

TEST_F(RootPresenterTest, InputInjection_MultipleRegistrationBySameDevice) {
  SetUpInputTest();

  fuchsia::ui::input::InputDevicePtr input_device_ptr;
  bool channel_error = false;
  input_device_ptr.set_error_handler([&channel_error](auto...) { channel_error = true; });
  input_device_registry_ptr_->RegisterDevice(TouchscreenDescriptorTemplate(),
                                             input_device_ptr.NewRequest());
  input_device_ptr->DispatchReport(TouchscreenReportTemplate());
  RunLoopUntilIdle();

  // After the first event a connection to the registry should have been made.
  EXPECT_EQ(injector_registry_->num_registered(), 1u);
  EXPECT_EQ(injector_registry_->num_events_received(), 1u);
  EXPECT_FALSE(channel_error);

  // Dispatch another event and then unregister the device by killing it.
  input_device_ptr->DispatchReport(TouchscreenReportTemplate());
  input_device_ptr.Unbind();

  // Register a new device with the same id.
  input_device_registry_ptr_->RegisterDevice(TouchscreenDescriptorTemplate(),
                                             input_device_ptr.NewRequest());
  input_device_ptr->DispatchReport(TouchscreenReportTemplate());

  // All pending messages should be worked through, and then the first device should disconnect from
  // the registry, while the second should remain connected.
  RunLoopUntilIdle();
  injector_registry_->FirePendingCallbacks();
  RunLoopUntilIdle();
  injector_registry_->FirePendingCallbacks();
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry_->num_registered(), 1u);
  EXPECT_EQ(injector_registry_->num_events_received(), 3u);
  EXPECT_FALSE(channel_error);
}

TEST_F(RootPresenterTest, InputInjection_FlowControl) {
  SetUpInputTest();

  fuchsia::ui::input::InputDevicePtr input_device_ptr;
  input_device_registry_ptr_->RegisterDevice(TouchscreenDescriptorTemplate(),
                                             input_device_ptr.NewRequest());
  input_device_ptr->DispatchReport(TouchscreenReportTemplate());
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry_->num_events_received(), 1u);

  // Next event gets buffered until the callback for the previous inejction returns.
  input_device_ptr->DispatchReport(TouchscreenReportTemplate());
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry_->num_events_received(), 1u);

  // After the callback the next event is immediately fired.
  injector_registry_->FirePendingCallbacks();
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry_->num_events_received(), 2u);
}

TEST_F(RootPresenterTest, InputInjection_EventBatching) {
  SetUpInputTest();

  fuchsia::ui::input::InputDevicePtr input_device_ptr;
  input_device_registry_ptr_->RegisterDevice(TouchscreenDescriptorTemplate(),
                                             input_device_ptr.NewRequest());
  input_device_ptr->DispatchReport(TouchscreenReportTemplate());
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry_->num_events_received(), 1u);

  // Buffer more events than can be injected in a single message.
  for (size_t i = 0; i < fuchsia::ui::pointerinjector::MAX_INJECT + 1; ++i) {
    input_device_ptr->DispatchReport(TouchscreenReportTemplate());
  }
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry_->num_events_received(), 1u);

  // After the callback, only kMaxEventsPerInjection should be sent.
  injector_registry_->FirePendingCallbacks();
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry_->num_events_received(),
            fuchsia::ui::pointerinjector::MAX_INJECT + 1);

  // And the last message should be sent after the next callback.
  injector_registry_->FirePendingCallbacks();
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry_->num_events_received(),
            fuchsia::ui::pointerinjector::MAX_INJECT + 2);
}

TEST_F(RootPresenterTest, InputInjection_InspectTouchscreen) {
  SetUpInputTest();

  fuchsia::ui::input::InputDevicePtr input_device_ptr;
  input_device_registry_ptr_->RegisterDevice(TouchscreenDescriptorTemplate(),
                                             input_device_ptr.NewRequest());
  input_device_ptr->DispatchReport(TouchscreenReportTemplate());
  RunLoopUntilIdle();

  // Check that the histograms are updated.
  {
    uint64_t count = 0;
    for (const inspect::UintArrayValue::HistogramBucket& bucket :
         GetHistogramBuckets({"input_reports"}, "touchscreen_latency")) {
      count += bucket.count;
    }
    EXPECT_EQ(1u, count);
  }
  {
    uint64_t count = 0;
    for (const inspect::UintArrayValue::HistogramBucket& bucket :
         GetHistogramBuckets({"presentation-0x0", "input_reports"}, "touchscreen_latency")) {
      count += bucket.count;
    }
    EXPECT_EQ(1u, count);
  }
  {
    uint64_t count = 0;
    for (const inspect::UintArrayValue::HistogramBucket& bucket :
         GetHistogramBuckets({"presentation-0x0", "input_events"}, "pointer_latency")) {
      count += bucket.count;
    }
    EXPECT_EQ(1u, count);
  }
}

TEST_F(RootPresenterTest, InputInjection_InspectMediaButtons) {
  SetUpInputTest();

  fuchsia::ui::input::InputDevicePtr input_device_ptr;
  input_device_registry_ptr_->RegisterDevice(MediaButtonsDescriptorTemplate(),
                                             input_device_ptr.NewRequest());
  input_device_ptr->DispatchReport(MediaButtonsReportTemplate());
  RunLoopUntilIdle();

  // Check that the histograms are updated.
  {
    uint64_t count = 0;
    for (const inspect::UintArrayValue::HistogramBucket& bucket :
         GetHistogramBuckets({"input_reports"}, "media_buttons_latency")) {
      count += bucket.count;
    }
    EXPECT_EQ(1u, count);
  }
}

// The below tests check that we recover correctly in the following scenarios:
// Registry closes the channel.
// Device is removed.
// Registry closes the channel and the device is removed at the same time.
TEST_F(RootPresenterTest, InputInjection_RecoverAndFinishStreamOnServerDisconnect) {
  SetUpInputTest();

  fuchsia::ui::input::InputDevicePtr input_device_ptr;
  bool channel_error = false;
  input_device_ptr.set_error_handler([&channel_error](auto...) { channel_error = true; });
  input_device_registry_ptr_->RegisterDevice(TouchscreenDescriptorTemplate(),
                                             input_device_ptr.NewRequest());

  {  // After the first event a connection to the registry should have been made.
    input_device_ptr->DispatchReport(TouchscreenReportTemplate());
    RunLoopUntilIdle();

    EXPECT_EQ(injector_registry_->num_registered(), 1u);
    EXPECT_EQ(injector_registry_->num_events_received(), 1u);
    EXPECT_FALSE(channel_error);
  }

  // Closing the channel on the other side should be transparent to InputDevice and a new connection
  // should be made immediately by the Presentation.
  injector_registry_->KillAllBindings();
  EXPECT_EQ(injector_registry_->num_registered(), 0u);
  RunLoopUntilIdle();
  EXPECT_FALSE(channel_error);
  EXPECT_EQ(injector_registry_->num_registered(), 1u);
}

TEST_F(RootPresenterTest, InputInjection_FinishStreamOnClientDisconnect) {
  SetUpInputTest();

  fuchsia::ui::input::InputDevicePtr input_device_ptr;
  input_device_registry_ptr_->RegisterDevice(TouchscreenDescriptorTemplate(),
                                             input_device_ptr.NewRequest());

  // Buffer an update.
  input_device_ptr->DispatchReport(TouchscreenReportTemplate());
  input_device_ptr->DispatchReport(TouchscreenReportTemplate());
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry_->num_events_received(), 1u);
  EXPECT_EQ(injector_registry_->num_registered(), 1u);

  // Killing the InputDevice-side channel should not be seen by the registry until the pending event
  // has been delivered and their callbacks returned.
  input_device_ptr.Unbind();
  EXPECT_EQ(injector_registry_->num_events_received(), 1u);
  EXPECT_EQ(injector_registry_->num_registered(), 1u);

  injector_registry_->FirePendingCallbacks();
  RunLoopUntilIdle();
  injector_registry_->FirePendingCallbacks();
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry_->num_events_received(), 2u);
  EXPECT_EQ(injector_registry_->num_registered(), 0u);
}

// Tests that Injector correctly buffers events until the scene is ready.
TEST_F(RootPresenterTest, InjectorStartupTest) {
  SetUpInputTest();

  auto [control_ref1, view_ref1] = scenic::ViewRefPair::New();
  auto [control_ref2, view_ref2] = scenic::ViewRefPair::New();
  input::Injector injector(context_provider_.context(), std::move(view_ref1), std::move(view_ref2));

  injector.OnDeviceAdded(/*device_id*/ 1);
  injector.OnDeviceAdded(/*device_id*/ 2);

  fuchsia::ui::input::InputEvent event;
  {
    fuchsia::ui::input::PointerEvent pointer;
    pointer.device_id = 1;
    pointer.pointer_id = 2;
    event.set_pointer(std::move(pointer));
  }
  injector.OnEvent(event);
  injector.OnEvent(event);

  // Remove and add device_id 1, to show that the injector
  // correctly buffers even on device_id reuse.
  injector.OnDeviceRemoved(/*device_id*/ 1);
  injector.OnDeviceAdded(/*device_id*/ 1);
  injector.OnEvent(event);

  RunLoopUntilIdle();

  EXPECT_EQ(injector_registry_->num_registered(), 0u);
  EXPECT_EQ(injector_registry_->num_events_received(), 0u);

  injector.MarkSceneReady();
  RunLoopUntilIdle();

  // All ongoing streams shold have registered and injected.
  EXPECT_EQ(injector_registry_->num_registered(), 3u);
  EXPECT_EQ(injector_registry_->num_events_received(), 3u);

  injector_registry_->FirePendingCallbacks();
  RunLoopUntilIdle();
  // The first injector for device_id 1 should have died.
  EXPECT_EQ(injector_registry_->num_registered(), 2u);

  // Any subsequent events should be handled immediately.
  injector.OnDeviceAdded(/*device_id*/ 3);
  injector.OnEvent(event);
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry_->num_registered(), 3u);
  EXPECT_EQ(injector_registry_->num_events_received(), 4u);
}

// Tests that focus is requested for the client after the client view is connected.
TEST_F(RootPresenterTest, FocusOnStartup) {
  ConnectAccessibilityViewRegistry();
  RunLoopUntilIdle();

  // Set up presentation.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [control_ref, view_ref] = scenic::ViewRefPair::New();
  const zx_koid_t child_view_koid = ExtractKoid(view_ref);

  fuchsia::ui::views::ViewRef clone;
  fidl::Clone(view_ref, &clone);
  presentation()->PresentOrReplaceView2(std::move(view_holder_token), std::move(clone), nullptr);
  RunLoopUntil([this]() { return presentation()->is_initialized(); });

  zx_koid_t keyboard_focus_view_koid = ZX_KOID_INVALID;
  // Callback to verify that a focus change triggered a notification.
  keyboard_focus_ctl_->SetOnNotify(
      [&keyboard_focus_view_koid](const fuchsia::ui::views::ViewRef& view_ref) {
        keyboard_focus_view_koid = ExtractKoid(view_ref);
      });

  // Connect to focus chain registry after Scenic has been set up.
  zx_koid_t focused_view_koid = ZX_KOID_INVALID;
  SetUpFocusChainListener([&focused_view_koid](fuchsia::ui::focus::FocusChain focus_chain) {
    if (!focus_chain.focus_chain().empty()) {
      focused_view_koid = ExtractKoid(focus_chain.focus_chain().back());
    }
  });

  // Create and connect child view.
  fuchsia::ui::scenic::ScenicPtr scenic;
  context_provider_.context()->svc()->Connect(scenic.NewRequest());
  scenic::Session session(scenic.get());
  session.Enqueue({scenic::NewCommand(scenic::NewCreateViewCmd(
      /*view_id*/ 1, std::move(view_token), std::move(control_ref), std::move(view_ref), ""))});
  session.Present(0, [](auto) {});

  // Expect focus to change to the child view.
  RunLoopUntil([&focused_view_koid, &keyboard_focus_view_koid, child_view_koid]() {
    return focused_view_koid == child_view_koid && keyboard_focus_view_koid == child_view_koid;
  });

  {  // Now reset and connect the A11y view and observe that focus again moves to the child view
     // once setup completes.
    focused_view_koid = ZX_KOID_INVALID;
    keyboard_focus_view_koid = ZX_KOID_INVALID;

    a11y::AccessibilityView a11y_view(context_provider_.context());
    RunLoopUntil([&a11y_view]() { return a11y_view.is_initialized(); });

    RunLoopUntil([&focused_view_koid, &keyboard_focus_view_koid, child_view_koid]() {
      return focused_view_koid == child_view_koid && keyboard_focus_view_koid == child_view_koid;
    });
  }
}

}  // namespace
}  // namespace root_presenter
