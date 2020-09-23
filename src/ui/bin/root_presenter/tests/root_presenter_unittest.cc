// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/dispatcher.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include "gtest/gtest.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/bin/root_presenter/app.h"
#include "src/ui/bin/root_presenter/presentation.h"
#include "src/ui/bin/root_presenter/tests/fakes/fake_injector_registry.h"

namespace root_presenter {
namespace {

class RootPresenterTest : public gtest::RealLoopFixture {
 public:
  void SetUp() final {
    real_component_context_ = sys::ComponentContext::CreateAndServeOutgoingDirectory();

    // Proxy real Scenic through the fake component_context.
    context_provider_.service_directory_provider()->AddService<fuchsia::ui::scenic::Scenic>(
        [this](fidl::InterfaceRequest<fuchsia::ui::scenic::Scenic> request) {
          real_component_context_->svc()->Connect(std::move(request));
        });

    injector_registry_ = std::make_unique<testing::FakeInjectorRegistry>(context_provider_);

    // Start RootPresenter with fake context.
    root_presenter_ = std::make_unique<App>(context_provider_.context(), dispatcher());
  }
  void TearDown() final { root_presenter_.reset(); }

  App* root_presenter() { return root_presenter_.get(); }

  void SetUpInputTest() {
    context_provider_.ConnectToPublicService<fuchsia::ui::input::InputDeviceRegistry>(
        input_device_registry_ptr_.NewRequest());
    input_device_registry_ptr_.set_error_handler([](auto...) { FAIL(); });

    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
    root_presenter()->PresentView(std::move(view_holder_token), nullptr);
    view_token_ = std::move(view_token);

    RunLoopUntil([this]() { return root_presenter()->is_presentation_initialized(); });
  }

  fuchsia::ui::input::DeviceDescriptor DeviceDescriptorTemplate() {
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

  fuchsia::ui::input::InputReport InputReportTemplate() {
    fuchsia::ui::input::InputReport input_report{
        .touchscreen = std::make_unique<fuchsia::ui::input::TouchscreenReport>()};
    input_report.touchscreen->touches.push_back(
        {.finger_id = 1, .x = 5, .y = 5, .width = 1, .height = 1});
    return input_report;
  }

  std::unique_ptr<testing::FakeInjectorRegistry> injector_registry_;
  fuchsia::ui::input::InputDeviceRegistryPtr input_device_registry_ptr_;
  sys::testing::ComponentContextProvider context_provider_;

 private:
  std::unique_ptr<sys::ComponentContext> real_component_context_;
  std::unique_ptr<App> root_presenter_;

  fuchsia::ui::views::ViewToken view_token_;
};

TEST_F(RootPresenterTest, SinglePresentView_ShouldSucceed) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  fidl::InterfacePtr<fuchsia::ui::policy::Presentation> presentation;
  bool alive = true;
  presentation.set_error_handler([&alive](auto) { alive = false; });
  root_presenter()->PresentView(std::move(view_holder_token), presentation.NewRequest());

  RunLoopUntilIdle();

  EXPECT_TRUE(alive);
}

TEST_F(RootPresenterTest, SecondPresentView_ShouldFail_AndOriginalShouldSurvive) {
  auto [view_token1, view_holder_token1] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();

  fidl::InterfacePtr<fuchsia::ui::policy::Presentation> presentation1;
  bool alive1 = true;
  presentation1.set_error_handler([&alive1](auto) { alive1 = false; });
  root_presenter()->PresentView(std::move(view_holder_token1), presentation1.NewRequest());

  fidl::InterfacePtr<fuchsia::ui::policy::Presentation> presentation2;
  bool alive2 = true;
  zx_status_t error = ZX_OK;
  presentation2.set_error_handler([&alive2, &error](zx_status_t err) {
    alive2 = false;
    error = err;
  });
  root_presenter()->PresentView(std::move(view_holder_token2), presentation2.NewRequest());

  RunLoopUntilIdle();

  EXPECT_TRUE(alive1);
  EXPECT_FALSE(alive2);
  EXPECT_EQ(error, ZX_ERR_ALREADY_BOUND)
      << "Should be: " << zx_status_get_string(ZX_ERR_ALREADY_BOUND)
      << " Was: " << zx_status_get_string(error);
}

TEST_F(RootPresenterTest, SinglePresentOrReplaceView_ShouldSucceeed) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  fidl::InterfacePtr<fuchsia::ui::policy::Presentation> presentation;
  bool alive = true;
  presentation.set_error_handler([&alive](auto) { alive = false; });
  root_presenter()->PresentView(std::move(view_holder_token), presentation.NewRequest());

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
  root_presenter()->PresentOrReplaceView(std::move(view_holder_token1), presentation1.NewRequest());

  fidl::InterfacePtr<fuchsia::ui::policy::Presentation> presentation2;
  bool alive2 = true;
  presentation2.set_error_handler([&alive2](auto) { alive2 = false; });
  root_presenter()->PresentOrReplaceView(std::move(view_holder_token2), presentation2.NewRequest());

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
  input_device_registry_ptr_->RegisterDevice(DeviceDescriptorTemplate(),
                                             input_device_ptr.NewRequest());
  ;
  input_device_ptr->DispatchReport(InputReportTemplate());
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
  input_device_registry_ptr_->RegisterDevice(DeviceDescriptorTemplate(),
                                             input_device_ptr.NewRequest());
  ;
  input_device_ptr->DispatchReport(InputReportTemplate());
  RunLoopUntilIdle();

  // After the first event a connection to the registry should have been made.
  EXPECT_EQ(injector_registry_->num_registered(), 1u);
  EXPECT_EQ(injector_registry_->num_events_received(), 1u);
  EXPECT_FALSE(channel_error);

  // Dispatch another event and then unregister the device by killing it.
  input_device_ptr->DispatchReport(InputReportTemplate());
  input_device_ptr.Unbind();

  // Register a new device with the same id.
  input_device_registry_ptr_->RegisterDevice(DeviceDescriptorTemplate(),
                                             input_device_ptr.NewRequest());
  input_device_ptr->DispatchReport(InputReportTemplate());

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
  input_device_registry_ptr_->RegisterDevice(DeviceDescriptorTemplate(),
                                             input_device_ptr.NewRequest());
  input_device_ptr->DispatchReport(InputReportTemplate());
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry_->num_events_received(), 1u);

  // Next event gets buffered until the callback for the previous inejction returns.
  input_device_ptr->DispatchReport(InputReportTemplate());
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
  input_device_registry_ptr_->RegisterDevice(DeviceDescriptorTemplate(),
                                             input_device_ptr.NewRequest());
  input_device_ptr->DispatchReport(InputReportTemplate());
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry_->num_events_received(), 1u);

  // Buffer more events than can be injected in a single message.
  for (size_t i = 0; i < fuchsia::ui::pointerinjector::MAX_INJECT + 1; ++i) {
    input_device_ptr->DispatchReport(InputReportTemplate());
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

// The below tests check that we recover correctly in the following scenarios:
// Registry closes the channel.
// Device is removed.
// Registry closes the channel and the device is removed at the same time.
TEST_F(RootPresenterTest, InputInjection_RecoverAndFinishStreamOnServerDisconnect) {
  SetUpInputTest();

  fuchsia::ui::input::InputDevicePtr input_device_ptr;
  bool channel_error = false;
  input_device_ptr.set_error_handler([&channel_error](auto...) { channel_error = true; });
  input_device_registry_ptr_->RegisterDevice(DeviceDescriptorTemplate(),
                                             input_device_ptr.NewRequest());

  {  // After the first event a connection to the registry should have been made.
    input_device_ptr->DispatchReport(InputReportTemplate());
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
  input_device_registry_ptr_->RegisterDevice(DeviceDescriptorTemplate(),
                                             input_device_ptr.NewRequest());

  // Buffer an update.
  input_device_ptr->DispatchReport(InputReportTemplate());
  input_device_ptr->DispatchReport(InputReportTemplate());
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

TEST_F(RootPresenterTest, InputInjection_FinishStreamOnServerAndClientDisconnect) {
  SetUpInputTest();

  fuchsia::ui::input::InputDevicePtr input_device_ptr;
  input_device_registry_ptr_->RegisterDevice(DeviceDescriptorTemplate(),
                                             input_device_ptr.NewRequest());

  // Buffer an update.
  input_device_ptr->DispatchReport(InputReportTemplate());
  input_device_ptr->DispatchReport(InputReportTemplate());
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry_->num_events_received(), 1u);
  EXPECT_EQ(injector_registry_->num_registered(), 1u);

  // After killing the server-side binding, the client should immediately reconnect and send
  // any events still pending.
  injector_registry_->KillAllBindings();
  // And if the client was disconnected at the same time, it should continue with clean up after
  // recovery.
  input_device_ptr.Unbind();
  EXPECT_EQ(injector_registry_->num_events_received(), 1u);
  EXPECT_EQ(injector_registry_->num_registered(), 0u);

  // The client should reconnect, send pending events and then clean up and close the channel to
  // the registry.
  RunLoopUntilIdle();
  EXPECT_EQ(injector_registry_->num_events_received(), 2u);
  EXPECT_EQ(injector_registry_->num_registered(), 0u);
}

// Tests that Injector correctly buffers events until the scene is ready.
TEST_F(RootPresenterTest, InjectorStartupTest) {
  auto [control_ref1, view_ref1] = scenic::ViewRefPair::New();
  auto [control_ref2, view_ref2] = scenic::ViewRefPair::New();
  Injector injector(context_provider_.context(), std::move(view_ref1), std::move(view_ref2));

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

}  // namespace
}  // namespace root_presenter
