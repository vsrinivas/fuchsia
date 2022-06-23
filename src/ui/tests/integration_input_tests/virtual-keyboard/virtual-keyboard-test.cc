// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/buildinfo/cpp/fidl.h>
#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/fonts/cpp/fidl.h>
#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/memorypressure/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <fuchsia/posix/socket/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/accessibility/view/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/observation/test/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <fuchsia/web/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>
#include <zircon/utc.h>

#include <iostream>
#include <type_traits>

#include <gtest/gtest.h>
#include <src/lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>
#include <test/virtualkeyboard/cpp/fidl.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

// This test exercises the virtual keyboard visibility interactions between Chromium and Root
// Presenter. It is a multi-component test, and carefully avoids sleeping or polling for component
// coordination.
// - It runs real Root Presenter and Scenic components.
// - It uses a fake display controller; the physical device is unused.
//
// Components involved
// - This test program
// - Root Presenter
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
using component_testing::RealmRoot;
using component_testing::Route;
using RealmBuilder = component_testing::RealmBuilder;

// Alias for Component child name as provided to Realm Builder.
using ChildName = std::string;

// Alias for Component Legacy URL as provided to Realm Builder.
using LegacyUrl = std::string;

// Max timeout in failure cases.
// Set this as low as you can that still works across all test platforms.
constexpr zx::duration kTimeout = zx::min(5);

constexpr auto kRootPresenter = "root_presenter";
constexpr auto kScenicTestRealm = "scenic-test-realm";
constexpr auto kResponseListener = "response_listener";

enum class TapLocation { kTopLeft, kTopRight };

// The type used to measure UTC time. The integer value here does not matter so
// long as it differs from the ZX_CLOCK_MONOTONIC=0 defined by Zircon.
using time_utc = zx::basic_time<1>;

void AddBaseComponents(RealmBuilder* realm_builder) {
  realm_builder->AddChild(kRootPresenter, "#meta/root_presenter.cm");
  realm_builder->AddChild(kScenicTestRealm, "#meta/scenic_only.cm");
}

void AddBaseRoutes(RealmBuilder* realm_builder) {
  // Capabilities routed from test_manager to components in realm.
  realm_builder->AddRoute(
      Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_},
                             Protocol{fuchsia::vulkan::loader::Loader::Name_},
                             Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                             Protocol{fuchsia::sysmem::Allocator::Name_},
                             Protocol{fuchsia::tracing::provider::Registry::Name_}},
            .source = ParentRef(),
            .targets = {ChildRef{kScenicTestRealm}}});
  realm_builder->AddRoute(
      Route{.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_},
                             Protocol{fuchsia::logger::LogSink::Name_}},
            .source = ParentRef(),
            .targets = {ChildRef{kRootPresenter}}});

  // Capabilities routed between siblings in realm.
  realm_builder->AddRoute(
      Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_},
                             Protocol{fuchsia::ui::pointerinjector::Registry::Name_},
                             Protocol{fuchsia::ui::focus::FocusChainListenerRegistry::Name_}},
            .source = ChildRef{kScenicTestRealm},
            .targets = {ChildRef{kRootPresenter}}});

  // Capabilities routed up to test driver (this component).
  realm_builder->AddRoute(
      Route{.capabilities = {Protocol{fuchsia::input::virtualkeyboard::Manager::Name_},
                             Protocol{fuchsia::input::virtualkeyboard::ControllerCreator::Name_},
                             Protocol{fuchsia::ui::input::InputDeviceRegistry::Name_},
                             Protocol{fuchsia::ui::accessibility::view::Registry::Name_},
                             Protocol{fuchsia::ui::policy::Presenter::Name_}},
            .source = ChildRef{kRootPresenter},
            .targets = {ParentRef()}});
  realm_builder->AddRoute(
      Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_},
                             Protocol{fuchsia::ui::observation::test::Registry::Name_}},
            .source = ChildRef{kScenicTestRealm},
            .targets = {ParentRef()}});
}

// Combines all vectors in `vecs` into one.
template <typename T>
std::vector<T> merge(std::initializer_list<std::vector<T>> vecs) {
  std::vector<T> result;
  for (auto v : vecs) {
    result.insert(result.end(), v.begin(), v.end());
  }
  return result;
}

bool CheckViewExistsInSnapshot(const fuchsia::ui::observation::geometry::ViewTreeSnapshot& snapshot,
                               zx_koid_t view_ref_koid) {
  if (!snapshot.has_views()) {
    return false;
  }

  auto snapshot_count = std::count_if(
      snapshot.views().begin(), snapshot.views().end(),
      [view_ref_koid](const auto& view) { return view.view_ref_koid() == view_ref_koid; });

  return snapshot_count > 0;
}

zx_koid_t ExtractKoid(const zx::object_base& object) {
  zx_info_handle_basic_t info{};
  if (object.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) != ZX_OK) {
    return ZX_KOID_INVALID;  // no info
  }

  return info.koid;
}

zx_koid_t ExtractKoid(const fuchsia::ui::views::ViewRef& view_ref) {
  return ExtractKoid(view_ref.reference);
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

class VirtualKeyboardBase : public gtest::RealLoopFixture {
 protected:
  VirtualKeyboardBase()
      : realm_builder_(std::make_unique<RealmBuilder>(RealmBuilder::Create())), realm_() {}

  ~VirtualKeyboardBase() override {
    FX_CHECK(injection_count_ > 0) << "injection expected but didn't happen.";
  }

  void SetUp() override {
    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTimeout);

    BuildRealm(this->GetTestComponents(), this->GetTestRoutes(), this->GetTestV2Components());

    // Get the display dimensions
    scenic_ = realm()->Connect<fuchsia::ui::scenic::Scenic>();
    scenic_->GetDisplayInfo([this](fuchsia::ui::gfx::DisplayInfo display_info) {
      display_width_ = display_info.width_in_px;
      display_height_ = display_info.height_in_px;
      FX_LOGS(INFO) << "Got display_width = " << *display_width_
                    << " and display_height = " << *display_height_;
    });
    RunLoopUntil([this] { return display_width_.has_value() && display_height_.has_value(); });
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

  // This method does NOT block; it only logs when the client view has rendered.
  void WatchClientRenderStatus(zx_koid_t client_view_ref_koid) {
    geometry_provider_->Watch([this, client_view_ref_koid](auto response) {
      if (response.has_updates() && !response.updates().empty() &&
          CheckViewExistsInSnapshot(response.updates().back(), client_view_ref_koid)) {
        FX_LOGS(INFO) << "Client view has rendered";
      } else {
        WatchClientRenderStatus(client_view_ref_koid);
      }
    });
  }

  // Launches the test client by connecting to fuchsia.ui.app.ViewProvider protocol.
  // This method should only be invoked if this protocol has been exposed from
  // the root of the test realm.
  void LaunchChromium() {
    // Use |fuchsia.ui.observation.test.Registry| to register the view observer endpoint with
    // scenic.
    realm()->Connect<fuchsia::ui::observation::test::Registry>(observer_registry_ptr_.NewRequest());
    observer_registry_ptr_->RegisterGlobalGeometryProvider(geometry_provider_.NewRequest());

    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

    auto root_presenter = realm()->Connect<fuchsia::ui::policy::Presenter>();
    root_presenter->PresentOrReplaceView(std::move(view_holder_token),
                                         /* presentation */ nullptr);

    auto [view_ref_control, view_ref] = scenic::ViewRefPair::New();
    const auto view_ref_koid = ExtractKoid(view_ref);

    WatchClientRenderStatus(view_ref_koid);

    auto view_provider = realm()->Connect<fuchsia::ui::app::ViewProvider>();
    view_provider->CreateViewWithViewRef(std::move(view_token.value), std::move(view_ref_control),
                                         std::move(view_ref));
  }

  // Inject directly into Root Presenter, using fuchsia.ui.input FIDLs.
  void InjectInput(int32_t x, int32_t y) {
    using fuchsia::ui::input::InputReport;
    // Device parameters
    auto parameters = std::make_unique<fuchsia::ui::input::TouchscreenDescriptor>();
    *parameters = {.x = {.range = {.min = 0, .max = static_cast<int32_t>(display_width())}},
                   .y = {.range = {.min = 0, .max = static_cast<int32_t>(display_height())}},
                   .max_finger_id = 10};
    FX_LOGS(INFO) << "Registering touchscreen with x touch range = (" << parameters->x.range.min
                  << ", " << parameters->x.range.max << ") "
                  << "and y touch range = (" << parameters->y.range.min << ", "
                  << parameters->y.range.max << ").";

    // Register it against Root Presenter.
    fuchsia::ui::input::DeviceDescriptor device{.touchscreen = std::move(parameters)};
    auto registry = realm()->Connect<fuchsia::ui::input::InputDeviceRegistry>();
    fuchsia::ui::input::InputDevicePtr connection;
    registry->RegisterDevice(std::move(device), connection.NewRequest());

    {
      // Inject input report.
      auto touch = std::make_unique<fuchsia::ui::input::TouchscreenReport>();
      *touch = {.touches = {{.finger_id = 1, .x = x, .y = y}}};
      InputReport report{.event_time = static_cast<uint64_t>(zx::clock::get_monotonic().get()),
                         .touchscreen = std::move(touch)};
      connection->DispatchReport(std::move(report));
      FX_LOGS(INFO) << "Dispatching touch report at (" << x << "," << y << ")";
    }

    {
      // Inject conclusion (empty) report.
      auto touch = std::make_unique<fuchsia::ui::input::TouchscreenReport>();
      InputReport report{.event_time = static_cast<uint64_t>(zx::clock::get_monotonic().get()),
                         .touchscreen = std::move(touch)};
      connection->DispatchReport(std::move(report));
    }

    ++injection_count_;
    FX_LOGS(INFO) << "*** Tap injected, count: " << injection_count_;
  }

  fuchsia::sys::ComponentControllerPtr& client_component() { return client_component_; }
  sys::ServiceDirectory& child_services() { return *child_services_; }

  RealmBuilder* builder() { return realm_builder_.get(); }
  RealmRoot* realm() { return realm_.get(); }

  InputPositionListenerServer* response_listener() { return response_listener_.get(); }

  // Guaranteed to be initialized after SetUp().
  uint32_t display_width() const { return *display_width_; }
  uint32_t display_height() const { return *display_height_; }

 private:
  void BuildRealm(const std::vector<std::pair<ChildName, LegacyUrl>>& components,
                  const std::vector<Route>& routes,
                  const std::vector<std::pair<ChildName, std::string>>& v2_components) {
    // Key part of service setup: have this test component vend the
    // |ResponseListener| service in the constructed realm.
    response_listener_ = std::make_unique<InputPositionListenerServer>(dispatcher());
    builder()->AddLocalChild(kResponseListener, response_listener_.get());

    // Add all components shared by each test to the realm.
    AddBaseComponents(builder());

    // Add components specific for this test case to the realm.
    for (const auto& [name, component] : components) {
      builder()->AddLegacyChild(name, component);
    }

    for (const auto& [name, component] : v2_components) {
      builder()->AddChild(name, component);
    }

    // Add the necessary routing for each of the base components added above.
    AddBaseRoutes(builder());

    // Add the necessary routing for each of the extra components added above.
    for (const auto& route : routes) {
      builder()->AddRoute(route);
    }

    // Finally, build the realm using the provided components and routes.
    realm_ = std::make_unique<RealmRoot>(builder()->Build());
  }

  std::unique_ptr<RealmBuilder> realm_builder_;
  std::unique_ptr<RealmRoot> realm_;

  std::unique_ptr<InputPositionListenerServer> response_listener_;

  std::unique_ptr<scenic::Session> session_;

  int injection_count_ = 0;

  fuchsia::ui::scenic::ScenicPtr scenic_;

  std::optional<uint32_t> display_width_;
  std::optional<uint32_t> display_height_;

  // Test view and child view's ViewHolder.
  std::unique_ptr<scenic::ViewHolder> view_holder_;
  std::unique_ptr<scenic::View> view_;

  fuchsia::ui::observation::test::RegistrySyncPtr observer_registry_ptr_;
  fuchsia::ui::observation::geometry::ProviderPtr geometry_provider_;

  fuchsia::sys::ComponentControllerPtr client_component_;
  std::shared_ptr<sys::ServiceDirectory> child_services_;
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
        std::make_pair(kBuildInfoProvider, kBuildInfoProviderUrl),
        std::make_pair(kFontsProvider, kFontsProviderUrl),
        std::make_pair(kIntl, kIntlUrl),
        std::make_pair(kMemoryPressureProvider, kMemoryPressureProviderUrl),
        std::make_pair(kNetstack, kNetstackUrl),
        std::make_pair(kSemanticsManager, kSemanticsManagerUrl),
        std::make_pair(kTextManager, kTextManagerUrl),
        std::make_pair(kWebVirtualKeyboardClient, kWebVirtualKeyboardUrl),
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
         .source = ChildRef{kTextManager},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::intl::PropertyProvider::Name_}},
         .source = ChildRef{kIntl},
         .targets = {target, ChildRef{kSemanticsManager}}},
        {.capabilities = {Protocol{fuchsia::input::virtualkeyboard::Manager::Name_},
                          Protocol{fuchsia::input::virtualkeyboard::ControllerCreator::Name_}},
         .source = ChildRef{kRootPresenter},
         .targets = {ChildRef{kWebVirtualKeyboardClient}}},
        {.capabilities = {Protocol{fuchsia::memorypressure::Provider::Name_}},
         .source = ChildRef{kMemoryPressureProvider},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                          Protocol{fuchsia::posix::socket::Provider::Name_},
                          Protocol{fuchsia::netstack::Netstack::Name_},
                          Protocol{fuchsia::net::interfaces::State::Name_}},
         .source = ChildRef{kNetstack},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::accessibility::semantics::SemanticsManager::Name_}},
         .source = ChildRef{kSemanticsManager},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::web::ContextProvider::Name_}},
         .source = ChildRef{kWebContextProvider},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_},
                          Protocol{fuchsia::ui::focus::FocusChainListenerRegistry::Name_}},
         .source = ChildRef{kScenicTestRealm},
         .targets = {ChildRef{kSemanticsManager}}},
        {.capabilities = {Protocol{fuchsia::cobalt::LoggerFactory::Name_}},
         .source = ChildRef{kScenicTestRealm},
         .targets = {ChildRef{kMemoryPressureProvider}}},
        {.capabilities = {Protocol{fuchsia::sysmem::Allocator::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kMemoryPressureProvider}, ChildRef{kWebVirtualKeyboardClient}}},
        {.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_},
                          Protocol{fuchsia::scheduler::ProfileProvider::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kMemoryPressureProvider}}},
        {.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
         .source = ChildRef{kScenicTestRealm},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::buildinfo::Provider::Name_}},
         .source = ChildRef{kBuildInfoProvider},
         .targets = {target, ChildRef{kWebContextProvider}}},
    };
  }

  static constexpr auto kWebVirtualKeyboardClient = "web_virtual_keyboard_client";
  static constexpr auto kWebVirtualKeyboardUrl = "#meta/web-virtual-keyboard-client.cm";

 private:
  static constexpr auto kFontsProvider = "fonts_provider";
  static constexpr auto kFontsProviderUrl = "#meta/fonts.cm";

  static constexpr auto kTextManager = "text_manager";
  static constexpr auto kTextManagerUrl = "#meta/text_manager.cm";

  static constexpr auto kIntl = "intl";
  static constexpr auto kIntlUrl = "#meta/intl_property_manager.cm";

  static constexpr auto kMemoryPressureProvider = "memory_pressure_provider";
  static constexpr auto kMemoryPressureProviderUrl = "#meta/memory_monitor.cm";

  static constexpr auto kNetstack = "netstack";
  static constexpr auto kNetstackUrl = "#meta/netstack.cm";

  static constexpr auto kWebContextProvider = "web_context_provider";
  static constexpr auto kWebContextProviderUrl =
      "fuchsia-pkg://fuchsia.com/web_engine#meta/context_provider.cmx";

  static constexpr auto kSemanticsManager = "semantics_manager";
  static constexpr auto kSemanticsManagerUrl = "#meta/fake-a11y-manager.cm";

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

TEST_F(WebEngineTest, ShowAndHideKeyboard) {
  LaunchChromium();
  client_component().events().OnTerminated = [](int64_t return_code,
                                                fuchsia::sys::TerminationReason reason) {
    if (return_code != 0) {
      FX_LOGS(FATAL) << "Web appterminated abnormally with return_code=" << return_code
                     << ", reason="
                     << static_cast<std::underlying_type_t<decltype(reason)>>(reason);
    }
  };

  FX_LOGS(INFO) << "Getting initial keyboard state";
  std::optional<bool> is_keyboard_visible;
  auto virtualkeyboard_manager = realm()->Connect<fuchsia::input::virtualkeyboard::Manager>();
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
  int32_t input_center_x = (input_pos.x0 + input_pos.x1) / 2;
  int32_t input_center_y = (input_pos.y0 + input_pos.y1) / 2;
  TryInject(input_center_x, input_center_y);

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
