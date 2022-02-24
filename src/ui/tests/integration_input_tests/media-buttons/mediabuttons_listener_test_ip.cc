// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/input/injection/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/ui/input/testing/fake_input_report_device/fake.h"
#include "src/ui/input/testing/fake_input_report_device/reports_reader.h"

namespace {
// Types imported for the realm_builder library.
using component_testing::ChildRef;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::RealmRoot;
using component_testing::Route;
using RealmBuilder = component_testing::RealmBuilder;

// Max timeout in failure cases.
// Set this as low as you can that still works across all test platforms.
constexpr zx::duration kTimeout = zx::min(5);

constexpr auto kInputTestRealm = "input-pipeline-test-realm";

// This implements the MediaButtonsListener class. Its purpose is to test that MediaButton Events
// are actually sent out to the Listeners.
class ButtonsListenerImpl : public fuchsia::ui::policy::MediaButtonsListener {
 public:
  ButtonsListenerImpl(
      fidl::InterfaceRequest<fuchsia::ui::policy::MediaButtonsListener> listener_request,
      fit::function<void(const fuchsia::ui::input::MediaButtonsEvent&)> on_event)
      : listener_binding_(this, std::move(listener_request)), on_event_(std::move(on_event)) {}

 private:
  // |MediaButtonsListener|
  void OnMediaButtonsEvent(fuchsia::ui::input::MediaButtonsEvent event) override {
    FX_NOTREACHED() << "unused";
  }

  // |MediaButtonsListener|
  void OnEvent(fuchsia::ui::input::MediaButtonsEvent event, OnEventCallback callback) override {
    on_event_(event);
    callback();
  }

  fidl::Binding<fuchsia::ui::policy::MediaButtonsListener> listener_binding_;
  fit::function<void(const fuchsia::ui::input::MediaButtonsEvent&)> on_event_;
};

class MediaButtonsListenerTest : public gtest::RealLoopFixture {
 protected:
  MediaButtonsListenerTest()
      : realm_builder_(std::make_unique<RealmBuilder>(RealmBuilder::Create())), realm_() {}

  ~MediaButtonsListenerTest() override {
    FX_CHECK(injection_count_ > 0) << "injection expected but didn't happen.";
  }

  void SetUp() override {
    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTimeout);

    // Add static test realm as a component to the realm.
    builder()->AddChild(kInputTestRealm, "#meta/input-pipeline-test-realm.cm");

    // Capabilities routed from test_manager to components in static test realm.
    builder()->AddRoute(
        Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_},
                               Protocol{fuchsia::vulkan::loader::Loader::Name_},
                               Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                               Protocol{fuchsia::sysmem::Allocator::Name_},
                               Protocol{fuchsia::tracing::provider::Registry::Name_}},
              .source = ParentRef(),
              .targets = {ChildRef{kInputTestRealm}}});

    // Capabilities routed from static test realm up to test driver (this component).
    builder()->AddRoute(
        Route{.capabilities =
                  {
                      Protocol{fuchsia::input::injection::InputDeviceRegistry::Name_},
                      Protocol{fuchsia::ui::policy::DeviceListenerRegistry::Name_},
                  },
              .source = ChildRef{kInputTestRealm},
              .targets = {ParentRef()}});

    // Finally, build the realm using the provided components and routes.
    realm_ = std::make_unique<RealmRoot>(builder()->Build());
  }

  void RegisterInjectionDevice() {
    registry_ = realm()->Connect<fuchsia::input::injection::InputDeviceRegistry>();

    // Create a FakeInputDevice
    fake_input_device_ = std::make_unique<fake_input_report_device::FakeInputDevice>(
        input_device_ptr_.NewRequest(), dispatcher());

    // Set descriptor
    auto device_descriptor = std::make_unique<fuchsia::input::report::DeviceDescriptor>();
    auto consumer_controls = device_descriptor->mutable_consumer_control()->mutable_input();
    consumer_controls->set_buttons({
        fuchsia::input::report::ConsumerControlButton::CAMERA_DISABLE,
        fuchsia::input::report::ConsumerControlButton::MIC_MUTE,
        fuchsia::input::report::ConsumerControlButton::PAUSE,
        fuchsia::input::report::ConsumerControlButton::VOLUME_UP,
        fuchsia::input::report::ConsumerControlButton::VOLUME_DOWN,
    });

    fake_input_device_->SetDescriptor(std::move(device_descriptor));

    // Register the FakeInputDevice
    registry_->Register(std::move(input_device_ptr_));
  }

  // Inject directly into Input Pipeline, using fuchsia.input.injection FIDLs.
  void InjectInput(fuchsia::input::report::ConsumerControlInputReport cc_input_report) {
    fuchsia::input::report::InputReport input_report;
    input_report.set_consumer_control(std::move(cc_input_report));

    std::vector<fuchsia::input::report::InputReport> input_reports;
    input_reports.push_back(std::move(input_report));
    fake_input_device_->SetReports(std::move(input_reports));

    ++injection_count_;
  }

  RealmBuilder* builder() { return realm_builder_.get(); }
  RealmRoot* realm() { return realm_.get(); }

 private:
  std::unique_ptr<RealmBuilder> realm_builder_;
  std::unique_ptr<RealmRoot> realm_;

  fuchsia::input::injection::InputDeviceRegistryPtr registry_;
  std::unique_ptr<fake_input_report_device::FakeInputDevice> fake_input_device_;
  fuchsia::input::report::InputDevicePtr input_device_ptr_;
  uint32_t injection_count_ = 0;
};

TEST_F(MediaButtonsListenerTest, MediaButtonsWithCallback) {
  RegisterInjectionDevice();

  // Callback to save the observed media button event.
  std::optional<fuchsia::ui::input::MediaButtonsEvent> observed_event;
  fit::function<void(const fuchsia::ui::input::MediaButtonsEvent&)> on_event =
      [&observed_event](const fuchsia::ui::input::MediaButtonsEvent& observed) {
        observed_event = fidl::Clone(observed);
      };

  // Register the MediaButtons listener against Input Pipeline and inject an event with pressed
  // buttons.
  fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> listener_handle;
  auto button_listener_impl =
      std::make_unique<ButtonsListenerImpl>(listener_handle.NewRequest(), std::move(on_event));

  auto listener_registry = realm()->Connect<fuchsia::ui::policy::DeviceListenerRegistry>();
  listener_registry.set_error_handler([](zx_status_t status) {
    FX_LOGS(FATAL) << "Lost connection to DeviceListenerRegistry: " << zx_status_get_string(status);
  });
  fuchsia::input::report::ConsumerControlInputReport first_report;
  first_report.set_pressed_buttons({
      fuchsia::input::report::ConsumerControlButton::CAMERA_DISABLE,
      fuchsia::input::report::ConsumerControlButton::MIC_MUTE,
      fuchsia::input::report::ConsumerControlButton::PAUSE,
      fuchsia::input::report::ConsumerControlButton::VOLUME_UP,
  });
  listener_registry->RegisterListener(
      std::move(listener_handle), [this, &first_report] { InjectInput(std::move(first_report)); });

  RunLoopUntil([&observed_event] { return observed_event.has_value(); });

  ASSERT_TRUE(observed_event->has_volume());
  EXPECT_EQ(observed_event->volume(), 1);
  ASSERT_TRUE(observed_event->has_mic_mute());
  EXPECT_TRUE(observed_event->mic_mute());
  ASSERT_TRUE(observed_event->has_pause());
  EXPECT_TRUE(observed_event->pause());
  ASSERT_TRUE(observed_event->has_camera_disable());
  EXPECT_TRUE(observed_event->camera_disable());

  // Inject a second event that represents releasing the pressed buttons.
  observed_event.reset();
  fuchsia::input::report::ConsumerControlInputReport second_report;
  second_report.set_pressed_buttons({});
  InjectInput(std::move(second_report));
  RunLoopUntil([&observed_event] { return observed_event.has_value(); });

  ASSERT_TRUE(observed_event->has_volume());
  EXPECT_EQ(observed_event->volume(), 0);
  ASSERT_TRUE(observed_event->has_mic_mute());
  EXPECT_FALSE(observed_event->mic_mute());
  ASSERT_TRUE(observed_event->has_pause());
  EXPECT_FALSE(observed_event->pause());
  ASSERT_TRUE(observed_event->has_camera_disable());
  EXPECT_FALSE(observed_event->camera_disable());
}

}  // namespace
