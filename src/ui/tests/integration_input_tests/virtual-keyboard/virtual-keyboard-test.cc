// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/buildinfo/cpp/fidl.h>
#include <fuchsia/fonts/cpp/fidl.h>
#include <fuchsia/input/injection/cpp/fidl.h>
#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/memorypressure/cpp/fidl.h>
#include <fuchsia/metrics/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <fuchsia/posix/socket/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/web/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/types.h>
#include <zircon/utc.h>

#include <iostream>
#include <type_traits>

#include <gtest/gtest.h>
#include <src/lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>
#include <test/virtualkeyboard/cpp/fidl.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/input/testing/fake_input_report_device/fake.h"
#include "src/ui/input/testing/fake_input_report_device/reports_reader.h"
#include "src/ui/testing/ui_test_manager/ui_test_manager.h"

// This test exercises the virtual keyboard visibility interactions between Chromium and Root
// Presenter. It is a multi-component test, and carefully avoids sleeping or polling for component
// coordination.
// - It runs real Root Presenter, Input Pipeline and Scenic components.
// - It uses a fake display controller; the physical device is unused.
//
// Components involved
// - This test program
// - Root Presenter (serves virtual keyboard)
// - Input Pipeline (serves touch input)
// - Scenic
// - WebEngine (built from Chromium)
//
// Setup sequence
// - The test sets up a view hierarchy with two views:
//   - Top level scene, owned by Root Presenter.
//   - Bottom view, owned by Chromium.

namespace {

using test::virtualkeyboard::InputPositionListener;
using ScenicEvent = fuchsia::ui::scenic::Event;
using GfxEvent = fuchsia::ui::gfx::Event;

// Types imported for the realm_builder library.
using component_testing::ChildRef;
using component_testing::Directory;
using component_testing::LocalComponent;
using component_testing::LocalComponentHandles;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Realm;
using component_testing::Route;

// Alias for Component child name as provided to Realm Builder.
using ChildName = std::string;

// Alias for Component Legacy URL as provided to Realm Builder.
using LegacyUrl = std::string;

constexpr auto kResponseListener = "response_listener";

// The type used to measure UTC time. The integer value here does not matter so
// long as it differs from the ZX_CLOCK_MONOTONIC=0 defined by Zircon.
using time_utc = zx::basic_time<1>;

// Combines all vectors in `vecs` into one.
template <typename T>
std::vector<T> merge(std::initializer_list<std::vector<T>> vecs) {
  std::vector<T> result;
  for (auto v : vecs) {
    result.insert(result.end(), v.begin(), v.end());
  }
  return result;
}

std::vector<ui_testing::UITestRealm::Config> UIConfigurationsToTest() {
  std::vector<ui_testing::UITestRealm::Config> configs;
  std::vector<std::string> protocols_required = {
      fuchsia::ui::scenic::Scenic::Name_,
      fuchsia::accessibility::semantics::SemanticsManager::Name_,
      fuchsia::ui::input3::Keyboard::Name_,
      fuchsia::ui::input::ImeService::Name_,
      fuchsia::input::virtualkeyboard::Manager::Name_,
      fuchsia::input::virtualkeyboard::ControllerCreator::Name_};

  // GFX x root presenter
  {
    ui_testing::UITestRealm::Config config;
    config.use_input = true;
    config.accessibility_owner = ui_testing::UITestRealm::AccessibilityOwnerType::FAKE;
    config.scene_owner = ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER;
    config.ui_to_client_services = protocols_required;
    configs.push_back(std::move(config));
  }

  // GFX x scene manager
  {
    ui_testing::UITestRealm::Config config;
    config.use_input = true;
    config.accessibility_owner = ui_testing::UITestRealm::AccessibilityOwnerType::FAKE;
    config.scene_owner = ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER;
    config.ui_to_client_services = protocols_required;
    configs.push_back(std::move(config));
  }

  // Flatland x scene manager
  {
    ui_testing::UITestRealm::Config config;
    config.use_input = true;
    config.use_flatland = true;
    config.accessibility_owner = ui_testing::UITestRealm::AccessibilityOwnerType::FAKE;
    config.scene_owner = ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER;
    config.ui_to_client_services = protocols_required;
    config.ui_to_client_services.push_back(fuchsia::ui::composition::Flatland::Name_);
    config.ui_to_client_services.push_back(fuchsia::ui::composition::Allocator::Name_);
    configs.push_back(std::move(config));
  }
  return configs;
}

// This component implements the interface for a RealmBuilder
// LocalComponent and the test.virtualkeyboard.InputPositionListener
// protocol.
class InputPositionListenerServer : public InputPositionListener, public LocalComponent {
 public:
  explicit InputPositionListenerServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  // |test::virtualkeyboard::InputPositionListener|
  void Notify(test::virtualkeyboard::BoundingBox bounding_box) override {
    input_position_ = std::move(bounding_box);
  }

  // |LocalComponent::Start|
  void Start(std::unique_ptr<LocalComponentHandles> local_handles) override {
    // When this component starts, add a binding to the test.touch.ResponseListener
    // protocol to this component's outgoing directory.
    FX_CHECK(local_handles->outgoing()->AddPublicService(
                 fidl::InterfaceRequestHandler<InputPositionListener>([this](auto request) {
                   bindings_.AddBinding(this, std::move(request), dispatcher_);
                 })) == ZX_OK);
    local_handle_ = std::move(local_handles);
  }

  const std::optional<test::virtualkeyboard::BoundingBox>& input_position() const {
    return input_position_;
  }

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  std::unique_ptr<LocalComponentHandles> local_handle_;
  std::optional<test::virtualkeyboard::BoundingBox> input_position_;
  fidl::BindingSet<InputPositionListener> bindings_;
};

class VirtualKeyboardBase : public gtest::RealLoopFixture,
                            public ::testing::WithParamInterface<ui_testing::UITestRealm::Config> {
 protected:
  VirtualKeyboardBase() = default;

  ~VirtualKeyboardBase() override {
    FX_CHECK(injection_count_ > 0) << "injection expected but didn't happen.";
  }

  void SetUp() override {
    FX_LOGS(INFO) << "Setting up test case";
    ui_test_manager_ = std::make_unique<ui_testing::UITestManager>(GetParam());

    // Build realm.
    FX_LOGS(INFO) << "Building realm";
    realm_ = std::make_unique<Realm>(ui_test_manager_->AddSubrealm());
    BuildRealm(this->GetTestComponents(), this->GetTestRoutes(), this->GetTestV2Components());

    // Get display dimensions.
    auto [width, height] = ui_test_manager_->GetDisplayDimensions();
    display_width_ = static_cast<uint32_t>(width);
    display_height_ = static_cast<uint32_t>(height);
    FX_LOGS(INFO) << "Got display_width = " << *display_width_
                  << " and display_height = " << *display_height_;

    RegisterInjectionDevice();
  }

  // Subclass should implement this method to add components to the test realm
  // next to the base ones added.
  virtual std::vector<std::pair<ChildName, LegacyUrl>> GetTestComponents() { return {}; }

  // Subclass should implement this method to add capability routes to the test
  // realm next to the base ones added.
  virtual std::vector<Route> GetTestRoutes() { return {}; }

  // Subclass should implement this method to add components to the test realm
  // next to the base ones added.
  virtual std::vector<std::pair<ChildName, std::string>> GetTestV2Components() { return {}; }

  void RegisterInjectionDevice() {
    registry_ = realm_exposed_services()->Connect<fuchsia::input::injection::InputDeviceRegistry>();
    registry_.set_error_handler([](zx_status_t status) {
      FX_LOGS(ERROR) << "Input device registry error: " << zx_status_get_string(status);
    });
    // Create a FakeInputDevice
    fake_input_device_ = std::make_unique<fake_input_report_device::FakeInputDevice>(
        input_device_ptr_.NewRequest(), dispatcher());

    // Set descriptor
    auto device_descriptor = std::make_unique<fuchsia::input::report::DeviceDescriptor>();
    auto touch = device_descriptor->mutable_touch()->mutable_input();
    touch->set_touch_type(fuchsia::input::report::TouchType::TOUCHSCREEN);
    touch->set_max_contacts(10);

    fuchsia::input::report::Axis x_axis;
    x_axis.unit.type = fuchsia::input::report::UnitType::NONE;
    x_axis.unit.exponent = 0;
    x_axis.range.min = 0;
    x_axis.range.max = display_width();

    fuchsia::input::report::Axis y_axis;
    y_axis.unit.type = fuchsia::input::report::UnitType::NONE;
    y_axis.unit.exponent = 0;
    y_axis.range.min = 0;
    y_axis.range.max = display_height();

    fuchsia::input::report::ContactInputDescriptor contact;
    contact.set_position_x(x_axis);
    contact.set_position_y(y_axis);
    contact.set_pressure(x_axis);

    touch->mutable_contacts()->push_back(std::move(contact));

    fake_input_device_->SetDescriptor(std::move(device_descriptor));

    // Register the FakeInputDevice
    registry_->Register(std::move(input_device_ptr_));
    FX_LOGS(INFO) << "Registered touchscreen with x touch range = (" << x_axis.range.min << ", "
                  << x_axis.range.max << ") "
                  << "and y touch range = (" << y_axis.range.min << ", " << y_axis.range.max
                  << ").";
  }

  // Inject directly into Input Pipeline, using fuchsia.input.injection FIDLs.
  void InjectInput(int32_t x, int32_t y) {
    fuchsia::input::report::ContactInputReport contact_input_report;
    contact_input_report.set_contact_id(1);
    contact_input_report.set_position_x(x);
    contact_input_report.set_position_y(y);

    fuchsia::input::report::TouchInputReport touch_input_report;
    auto contacts = touch_input_report.mutable_contacts();
    contacts->push_back(std::move(contact_input_report));

    fuchsia::input::report::InputReport input_report;
    input_report.set_touch(std::move(touch_input_report));

    std::vector<fuchsia::input::report::InputReport> input_reports;
    input_reports.push_back(std::move(input_report));

    fuchsia::input::report::TouchInputReport remove_touch_input_report;
    fuchsia::input::report::InputReport remove_input_report;
    remove_input_report.set_touch(std::move(remove_touch_input_report));
    input_reports.push_back(std::move(remove_input_report));
    fake_input_device_->SetReports(std::move(input_reports));

    ++injection_count_;
    FX_LOGS(INFO) << "*** Tap injected, count: " << injection_count_;
  }

  InputPositionListenerServer* response_listener() { return response_listener_.get(); }

  // Guaranteed to be initialized after SetUp().
  uint32_t display_width() const { return *display_width_; }
  uint32_t display_height() const { return *display_height_; }

  ui_testing::UITestManager* ui_test_manager() { return ui_test_manager_.get(); }
  sys::ServiceDirectory* realm_exposed_services() { return realm_exposed_services_.get(); }

 private:
  void BuildRealm(const std::vector<std::pair<ChildName, LegacyUrl>>& components,
                  const std::vector<Route>& routes,
                  const std::vector<std::pair<ChildName, std::string>>& v2_components) {
    // Key part of service setup: have this test component vend the
    // |ResponseListener| service in the constructed realm.
    response_listener_ = std::make_unique<InputPositionListenerServer>(dispatcher());
    realm_->AddLocalChild(kResponseListener, response_listener_.get());

    // Add components specific for this test case to the realm.
    for (const auto& [name, component] : components) {
      realm_->AddLegacyChild(name, component);
    }

    for (const auto& [name, component] : v2_components) {
      realm_->AddChild(name, component);
    }

    // Add the necessary routing for each of the extra components added above.
    for (const auto& route : routes) {
      realm_->AddRoute(route);
    }

    ui_test_manager_->BuildRealm();
    realm_exposed_services_ = ui_test_manager_->CloneExposedServicesDirectory();
  }

  // Configures a RealmBuilder realm and manages scene on behalf of the test
  // fixture.
  std::unique_ptr<ui_testing::UITestManager> ui_test_manager_;

  // Exposed services directory for the realm owned by `ui_test_manager_`.
  std::unique_ptr<sys::ServiceDirectory> realm_exposed_services_;

  // Configured by the test fixture, and attached as a subrealm to ui test
  // manager's realm.
  std::unique_ptr<Realm> realm_;

  std::unique_ptr<InputPositionListenerServer> response_listener_;

  int injection_count_ = 0;

  std::optional<uint32_t> display_width_;
  std::optional<uint32_t> display_height_;

  fuchsia::input::injection::InputDeviceRegistryPtr registry_;
  std::unique_ptr<fake_input_report_device::FakeInputDevice> fake_input_device_;
  fuchsia::input::report::InputDevicePtr input_device_ptr_;
};

class WebEngineTest : public VirtualKeyboardBase {
 protected:
  std::vector<std::pair<ChildName, LegacyUrl>> GetTestComponents() override {
    return {
        std::make_pair(kWebContextProvider, kWebContextProviderUrl),
    };
  }

  std::vector<std::pair<ChildName, LegacyUrl>> GetTestV2Components() override {
    return {
        std::make_pair(kWebVirtualKeyboardClient, kWebVirtualKeyboardUrl),
        std::make_pair(kBuildInfoProvider, kBuildInfoProviderUrl),
        std::make_pair(kFontsProvider, kFontsProviderUrl),
        std::make_pair(kIntl, kIntlUrl),
        std::make_pair(kMemoryPressureProvider, kMemoryPressureProviderUrl),
        std::make_pair(kMockCobalt, kMockCobaltUrl),
        std::make_pair(kNetstack, kNetstackUrl),
    };
  }

  std::vector<Route> GetTestRoutes() override {
    return merge({
        GetWebEngineRoutes(ChildRef{kWebVirtualKeyboardClient}),
        {
            {
                .capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                .source = ChildRef{kWebVirtualKeyboardClient},
                .targets = {ParentRef()},
            },
            {
                .capabilities = {Protocol{fuchsia::logger::LogSink::Name_}},
                .source = ParentRef(),
                .targets = {ChildRef{kWebVirtualKeyboardClient}},
            },
        },
    });
  }

  // Injects an input event, and posts a task to retry after `kTapRetryInterval`.
  //
  // We post the retry task because the first input event we send to WebEngine may be lost.
  // There is no guarantee that, just because the web app has returned the location of the
  // input box, that Chromium is actually ready to receive events from Scenic.
  void TryInject(int32_t x, int32_t y) {
    InjectInput(x, y);
    inject_retry_task_.emplace(
        [this, x, y](auto dispatcher, auto task, auto status) { TryInject(x, y); });
    FX_CHECK(inject_retry_task_->PostDelayed(dispatcher(), kTapRetryInterval) == ZX_OK);
  }

  void CancelInject() { inject_retry_task_.reset(); }

  // Routes needed to setup Chromium client.
  static std::vector<Route> GetWebEngineRoutes(ChildRef target) {
    return {
        {.capabilities = {Protocol{test::virtualkeyboard::InputPositionListener::Name_}},
         .source = ChildRef{kResponseListener},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::fonts::Provider::Name_}},
         .source = ChildRef{kFontsProvider},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_},
                          Protocol{fuchsia::logger::LogSink::Name_},
                          Directory{.name = "config-data",
                                    .rights = fuchsia::io::R_STAR_DIR,
                                    .path = "/config/data"}},
         .source = ParentRef(),
         .targets = {ChildRef{kFontsProvider}}},
        {.capabilities = {Protocol{fuchsia::ui::input3::Keyboard::Name_},
                          Protocol{fuchsia::ui::input::ImeService::Name_}},
         .source = ParentRef(),
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::intl::PropertyProvider::Name_}},
         .source = ChildRef{kIntl},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::input::virtualkeyboard::Manager::Name_},
                          Protocol{fuchsia::input::virtualkeyboard::ControllerCreator::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kWebVirtualKeyboardClient}, ChildRef{kWebContextProvider}}},
        {.capabilities = {Protocol{fuchsia::memorypressure::Provider::Name_}},
         .source = ChildRef{kMemoryPressureProvider},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::posix::socket::Provider::Name_},
                          Protocol{fuchsia::netstack::Netstack::Name_},
                          Protocol{fuchsia::net::interfaces::State::Name_}},
         .source = ChildRef{kNetstack},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::accessibility::semantics::SemanticsManager::Name_}},
         .source = ParentRef(),
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::web::ContextProvider::Name_}},
         .source = ChildRef{kWebContextProvider},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::metrics::MetricEventLoggerFactory::Name_}},
         .source = ChildRef{kMockCobalt},
         .targets = {ChildRef{kMemoryPressureProvider}}},
        {.capabilities = {Protocol{fuchsia::sysmem::Allocator::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kMemoryPressureProvider}, ChildRef{kWebVirtualKeyboardClient}}},
        {.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_},
                          Protocol{fuchsia::scheduler::ProfileProvider::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kMemoryPressureProvider}}},
        {.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
         .source = ParentRef(),
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::ui::composition::Flatland::Name_},
                          Protocol{fuchsia::ui::composition::Allocator::Name_}},
         .source = ParentRef(),
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::buildinfo::Provider::Name_}},
         .source = ChildRef{kBuildInfoProvider},
         .targets = {target, ChildRef{kWebContextProvider}}},
        {
            .capabilities = {Protocol{fuchsia::logger::LogSink::Name_}},
            .source = ParentRef(),
            .targets = {ChildRef{kWebContextProvider}},
        },
    };
  }

 private:
  static constexpr auto kMockCobalt = "cobalt";
  static constexpr auto kMockCobaltUrl = "#meta/mock_cobalt.cm";

  static constexpr auto kWebVirtualKeyboardClient = "web_virtual_keyboard_client";
  static constexpr auto kWebVirtualKeyboardUrl = "#meta/web-virtual-keyboard-client.cm";

  static constexpr auto kFontsProvider = "fonts_provider";
  static constexpr auto kFontsProviderUrl = "#meta/fonts.cm";

  static constexpr auto kIntl = "intl";
  static constexpr auto kIntlUrl = "#meta/intl_property_manager.cm";

  static constexpr auto kMemoryPressureProvider = "memory_pressure_provider";
  static constexpr auto kMemoryPressureProviderUrl = "#meta/memory_monitor.cm";

  static constexpr auto kNetstack = "netstack";
  static constexpr auto kNetstackUrl = "#meta/netstack.cm";

  static constexpr auto kWebContextProvider = "web_context_provider";
  static constexpr auto kWebContextProviderUrl =
      "fuchsia-pkg://fuchsia.com/web_engine#meta/context_provider.cmx";

  static constexpr auto kBuildInfoProvider = "build_info_provider";
  static constexpr auto kBuildInfoProviderUrl = "#meta/fake_build_info.cm";

  // The typical latency on devices we've tested is ~60 msec. The retry interval is chosen to be
  // a) Long enough that it's unlikely that we send a new tap while a previous tap is still being
  //    processed. That is, it should be far more likely that a new tap is sent because the first
  //    tap was lost, than because the system is just running slowly.
  // b) Short enough that we don't slow down tryjobs.
  //
  // The first property is important to avoid skewing the latency metrics that we collect.
  // For an explanation of why a tap might be lost, see the documentation for TryInject().
  static constexpr auto kTapRetryInterval = zx::sec(1);

  std::optional<async::Task> inject_retry_task_;
};

INSTANTIATE_TEST_SUITE_P(WebEngineTestWithParams, WebEngineTest,
                         ::testing::ValuesIn(UIConfigurationsToTest()));
TEST_P(WebEngineTest, ShowAndHideKeyboard) {
  // Launch the chromium view.
  ui_test_manager()->InitializeScene();
  FX_LOGS(INFO) << "Waiting for client view to render";
  RunLoopUntil([this] { return ui_test_manager()->ClientViewIsRendering(); });

  FX_LOGS(INFO) << "Waiting for client view to be focused";
  RunLoopUntil([this] { return ui_test_manager()->ClientViewIsFocused(); });

  FX_LOGS(INFO) << "Getting initial keyboard state";
  std::optional<bool> is_keyboard_visible;
  auto virtualkeyboard_manager =
      realm_exposed_services()->Connect<fuchsia::input::virtualkeyboard::Manager>();
  virtualkeyboard_manager->WatchTypeAndVisibility(
      [&is_keyboard_visible](auto text_type, auto is_visible) {
        is_keyboard_visible = is_visible;
      });
  RunLoopUntil([&]() { return is_keyboard_visible.has_value(); });
  ASSERT_FALSE(is_keyboard_visible.value());
  is_keyboard_visible.reset();

  FX_LOGS(INFO) << "Getting input box position";
  RunLoopUntil([this]() { return response_listener()->input_position().has_value(); });

  FX_LOGS(INFO) << "Tapping _inside_ input box";
  auto input_pos = *response_listener()->input_position();
  int32_t input_center_x_local = (input_pos.x0 + input_pos.x1) / 2;
  int32_t input_center_y_local = (input_pos.y0 + input_pos.y1) / 2;
  TryInject(input_center_x_local, input_center_y_local);

  FX_LOGS(INFO) << "Waiting for keyboard to be visible";
  virtualkeyboard_manager->WatchTypeAndVisibility(
      [&is_keyboard_visible](auto text_type, auto is_visible) {
        is_keyboard_visible = is_visible;
      });
  RunLoopUntil([&]() { return is_keyboard_visible.has_value(); });
  ASSERT_TRUE(is_keyboard_visible.value());
  CancelInject();
  is_keyboard_visible.reset();

  FX_LOGS(INFO) << "Tapping _outside_ input box";
  TryInject(input_pos.x1 + 1, input_pos.y1 + 1);

  FX_LOGS(INFO) << "Waiting for keyboard to be hidden";
  virtualkeyboard_manager->WatchTypeAndVisibility(
      [&is_keyboard_visible](auto text_type, auto is_visible) {
        is_keyboard_visible = is_visible;
      });
  RunLoopUntil([&]() { return is_keyboard_visible.has_value(); });
  ASSERT_FALSE(is_keyboard_visible.value());
  CancelInject();
}

}  // namespace
