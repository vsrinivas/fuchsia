// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/session/scene/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <fuchsia/web/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_creation_tokens.h>
#include <lib/ui/scenic/cpp/view_identity.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
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
#include <test/inputsynthesis/cpp/fidl.h>
#include <test/mouse/cpp/fidl.h>

namespace {

using fuchsia::ui::app::CreateView2Args;
using fuchsia::ui::app::ViewProvider;
using fuchsia::ui::composition::ChildViewStatus;
using fuchsia::ui::composition::ChildViewWatcher;
using fuchsia::ui::composition::ContentId;
using fuchsia::ui::composition::LayoutInfo;
using fuchsia::ui::composition::ParentViewportStatus;
using fuchsia::ui::composition::ParentViewportWatcher;
using fuchsia::ui::composition::PresentArgs;
using fuchsia::ui::composition::TransformId;
using fuchsia::ui::composition::ViewportProperties;
using fuchsia::ui::views::ViewRef;
using fuchsia::ui::views::ViewRefControl;

// Types imported for the realm_builder library.
using sys::testing::ChildRef;
using sys::testing::LocalComponent;
using sys::testing::LocalComponentHandles;
using sys::testing::ParentRef;
using sys::testing::Protocol;
using sys::testing::Route;
using sys::testing::experimental::RealmRoot;
using RealmBuilder = sys::testing::experimental::RealmBuilder;

// Alias for Component child name as provided to Realm Builder.
using ChildName = std::string;

// Alias for Component Legacy URL as provided to Realm Builder.
using LegacyUrl = std::string;

// Max timeout in failure cases.
// Set this as low as you can that still works across all test platforms.
constexpr zx::duration kTimeout = zx::min(5);

// Combines all vectors in `vecs` into one.
template <typename T>
std::vector<T> merge(std::initializer_list<std::vector<T>> vecs) {
  std::vector<T> result;
  for (auto v : vecs) {
    result.insert(result.end(), v.begin(), v.end());
  }
  return result;
}

// This is an in-process server for the `fuchsia.ui.app.ViewProvider` API for this
// test.  It is required for this test to be able to define and set up its view
// as the root view in Scenic's scene graph.  The implementation does little more
// than to provide correct wiring of the FIDL API.  The test that uses it is
// expected to provide a closure via SetCreateView2Callback, which will get invoked
// when a message is received.
//
// Only Flatland methods are implemented, others will cause the server to crash
// the test deliberately.
class ViewProviderServer : public ViewProvider, public LocalComponent {
 public:
  explicit ViewProviderServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  // Start serving `ViewProvider` for the stream that arrives via `request`.
  void Bind(fidl::InterfaceRequest<ViewProvider> request) {
    bindings_.AddBinding(this, std::move(request), dispatcher_);
  }

  // Set this callback to direct where an incoming message from `CreateView2` will
  // get forwarded to.
  void SetCreateView2Callback(std::function<void(CreateView2Args)> callback) {
    create_view2_callback_ = std::move(callback);
  }

  // LocalComponent::Start
  void Start(std::unique_ptr<LocalComponentHandles> mock_handles) override {
    // When this component starts, add a binding to the fuchsia.ui.app.ViewProvider
    // protocol to this component's outgoing directory.
    FX_CHECK(mock_handles->outgoing()->AddPublicService(
                 fidl::InterfaceRequestHandler<ViewProvider>([this](auto request) {
                   bindings_.AddBinding(this, std::move(request), dispatcher_);
                 })) == ZX_OK);
    mock_handles_.emplace_back(std::move(mock_handles));
  }

  // Gfx protocol is not implemented.
  void CreateView(
      ::zx::eventpair token,
      ::fidl::InterfaceRequest<::fuchsia::sys::ServiceProvider> incoming_services,
      ::fidl::InterfaceHandle<::fuchsia::sys::ServiceProvider> outgoing_services) override {
    FAIL() << "CreateView is not supported.";
  }

  // Gfx protocol is not implemented.
  void CreateViewWithViewRef(::zx::eventpair token, ViewRefControl view_ref_control,
                             ViewRef view_ref) override {
    FAIL() << "CreateViewWithRef is not supported.";
  }

  // Implements server-side `fuchsia.ui.app.ViewProvider/CreateView2`
  void CreateView2(CreateView2Args args) override { create_view2_callback_(std::move(args)); }

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  std::vector<std::unique_ptr<LocalComponentHandles>> mock_handles_;
  fidl::BindingSet<ViewProvider> bindings_;

  std::function<void(CreateView2Args)> create_view2_callback_ = nullptr;
};

// `ResponseListener` is a local test protocol that our test Flutter app uses to let us know
// what position and button press state the mouse cursor has.
class ResponseListenerServer : public test::mouse::ResponseListener, public LocalComponent {
 public:
  explicit ResponseListenerServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  // |test::mouse::ResponseListener|
  void Respond(test::mouse::PointerData pointer_data) override {
    FX_CHECK(respond_callback_) << "Expected callback to be set for test.mouse.Respond().";
    respond_callback_(std::move(pointer_data));
  }

  // |MockComponent::Start|
  // When the component framework requests for this component to start, this
  // method will be invoked by the realm_builder library.
  void Start(std::unique_ptr<LocalComponentHandles> mock_handles) override {
    // When this component starts, add a binding to the test.mouse.ResponseListener
    // protocol to this component's outgoing directory.
    FX_CHECK(mock_handles->outgoing()->AddPublicService(
                 fidl::InterfaceRequestHandler<test::mouse::ResponseListener>([this](auto request) {
                   bindings_.AddBinding(this, std::move(request), dispatcher_);
                 })) == ZX_OK);
    mock_handles_.emplace_back(std::move(mock_handles));
  }

  void SetRespondCallback(fit::function<void(test::mouse::PointerData)> callback) {
    respond_callback_ = std::move(callback);
  }

 private:
  // Not owned.
  async_dispatcher_t* dispatcher_ = nullptr;
  fidl::BindingSet<test::mouse::ResponseListener> bindings_;
  std::vector<std::unique_ptr<LocalComponentHandles>> mock_handles_;
  fit::function<void(test::mouse::PointerData)> respond_callback_;
};

// A minimal server for fuchsia.ui.composition.ParentViewportWatcher.  All it
// does is forward the values it receives to the functions set by the user.
class ParentViewportWatcherClient {
 public:
  struct Callbacks {
    // Called when GetLayout returns.
    std::function<void(LayoutInfo info)> on_get_layout{};
    // Called when GetStatus returns.
    std::function<void(ParentViewportStatus info)> on_status_info{};
  };
  explicit ParentViewportWatcherClient(fidl::InterfaceHandle<ParentViewportWatcher> client_end,
                                       Callbacks callbacks)
      // Subtle: callbacks are initialized before a call to Bind, so that we don't
      // receive messages from the client end before the callbacks are installed.
      : callbacks_(std::move(callbacks)), client_end_(client_end.Bind()) {
    client_end_.set_error_handler([](zx_status_t status) {
      FX_LOGS(ERROR) << "watcher error: " << zx_status_get_string(status);
    });
    // Kick off hanging get requests now.
    ScheduleGetLayout();
    ScheduleStatusInfo();
  }

 private:
  // Schedule* methods ensure that changes to the status are continuously
  // communicated to the test fixture. This is because the statuses may
  // change several times before they settle into the value we need.

  void ScheduleGetLayout() {
    client_end_->GetLayout([this](LayoutInfo l) {
      this->callbacks_.on_get_layout(std::move(l));
      ScheduleGetLayout();
    });
  }

  void ScheduleStatusInfo() {
    client_end_->GetStatus([this](ParentViewportStatus s) {
      this->callbacks_.on_status_info(s);
      ScheduleStatusInfo();
    });
  }

  Callbacks callbacks_;
  fidl::InterfacePtr<ParentViewportWatcher> client_end_;
};

// A minimal server for fuchsia.ui.composition.ChildViewWatcher.  All it does is
// forward the values it receives to the functions set by the user.
class ChildViewWatcherClient {
 public:
  struct Callbacks {
    // Called when GetStatus returns.
    std::function<void(ChildViewStatus)> on_get_status{};
    // Called when GetViewRef returns.
    std::function<void(ViewRef)> on_get_view_ref{};
  };

  explicit ChildViewWatcherClient(fidl::InterfaceHandle<ChildViewWatcher> client_end,
                                  Callbacks callbacks)
      // Subtle: callbacks need to be initialized before a call to Bind, else
      // we may receive messages before we install the message handlers.
      : callbacks_(std::move(callbacks)), client_end_(client_end.Bind()) {
    client_end_.set_error_handler([](zx_status_t status) {
      FX_LOGS(ERROR) << "watcher error: " << zx_status_get_string(status);
    });
    ScheduleGetStatus();
    ScheduleGetViewRef();
  }

 private:
  // Schedule* methods ensure that changes to the status are continuously
  // communicated to the test fixture. This is because the statuses may
  // change several times before they settle into the value we need.

  void ScheduleGetStatus() {
    client_end_->GetStatus([this](ChildViewStatus status) {
      this->callbacks_.on_get_status(status);
      this->ScheduleGetStatus();
    });
  }

  void ScheduleGetViewRef() {
    client_end_->GetViewRef([this](ViewRef view_ref) {
      this->callbacks_.on_get_view_ref(std::move(view_ref));
      this->ScheduleGetViewRef();
    });
  }

  Callbacks callbacks_;
  fidl::InterfacePtr<ChildViewWatcher> client_end_;
};

constexpr auto kTestRealm = "workstation-test-realm";
constexpr auto kResponseListener = "response_listener";

class MouseInputBase : public gtest::RealLoopFixture {
 protected:
  MouseInputBase()
      : realm_builder_(std::make_unique<RealmBuilder>(RealmBuilder::Create())),
        view_provider_server_(std::make_unique<ViewProviderServer>(dispatcher())),
        response_listener_(std::make_unique<ResponseListenerServer>(dispatcher())) {}

  RealmBuilder* builder() { return realm_builder_.get(); }
  RealmRoot* realm() { return realm_.get(); }

  ResponseListenerServer* response_listener() { return response_listener_.get(); }

  void SetUp() override {
    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTimeout);

    AssembleRealm(this->GetTestComponents(), this->GetTestRoutes());
  }

  // Subclass should implement this method to add components to the test realm
  // next to the base ones added.
  virtual std::vector<std::pair<ChildName, LegacyUrl>> GetTestComponents() { return {}; }

  // Subclass should implement this method to add capability routes to the test
  // realm next to the base ones added.
  virtual std::vector<Route> GetTestRoutes() { return {}; }

  // Launches the test client by connecting to ViewProviderServer.
  void LaunchClient(std::string debug_name) {
    auto scene_manager = realm_->Connect<fuchsia::session::scene::Manager>();

    fidl::InterfaceHandle<ViewProvider> view_provider_handle;
    view_provider_server_->Bind(view_provider_handle.NewRequest());

    std::optional<fuchsia::ui::app::CreateView2Args> args;
    view_provider_server_->SetCreateView2Callback(
        [&](fuchsia::ui::app::CreateView2Args a) { args = std::move(a); });

    std::optional<ViewRef> view_ref_from_scene;
    scene_manager->SetRootView(
        std::move(view_provider_handle),
        [&view_ref_from_scene](ViewRef ref) { view_ref_from_scene = std::move(ref); });
    FX_LOGS(INFO) << "Waiting for args";
    RunLoopUntil([&] { return args.has_value(); });

    // Connect to the scene graph.
    auto flatland = realm_->Connect<fuchsia::ui::composition::Flatland>();
    flatland.set_error_handler([](zx_status_t status) {
      FX_LOGS(ERROR) << "Flatland error status: " << zx_status_get_string(status);
    });
    flatland->SetDebugName(debug_name);

    fidl::InterfaceHandle<fuchsia::ui::composition::ParentViewportWatcher> parent_watcher;
    auto view_identity = scenic::NewViewIdentityOnCreation();
    flatland->CreateView2(std::move(*args.value().mutable_view_creation_token()),
                          std::move(view_identity), {}, parent_watcher.NewRequest());

    std::optional<LayoutInfo> layout_info;
    std::optional<ParentViewportStatus> status_info;
    ParentViewportWatcherClient parent_watcher_client{
        std::move(parent_watcher),
        ParentViewportWatcherClient::Callbacks{
            .on_get_layout =
                [&layout_info](LayoutInfo l) {
                  FX_VLOGS(1) << "OnGetLayout message received.";
                  layout_info = std::move(l);
                },
            .on_status_info =
                [&status_info](ParentViewportStatus s) {
                  FX_VLOGS(1) << "SetOnStatusInfo message received.";
                  status_info = s;
                },
        },
    };

    // Subtle: OnGetLayout can return before a call to Present is made.
    // OnStatusInfo may not return until after a call to Present is made.
    FX_LOGS(INFO) << "Waiting for layout information";
    RunLoopUntil([&] { return layout_info.has_value(); });

    // A transform must exist on the view in order for the connection to be
    // established properly. Set it up here.  The ID is set to an arbitrary
    // number.
    flatland->CreateTransform(TransformId{.value = 42});
    flatland->SetRootTransform(TransformId{.value = 42});

    // Commit all previously scheduled operations.
    flatland->Present(PresentArgs{});

    FX_LOGS(INFO) << "Waiting for status info.";
    RunLoopUntil([&] {
      return status_info.has_value() &&
             status_info.value() == ParentViewportStatus::CONNECTED_TO_DISPLAY;
    });

    FX_LOGS(INFO) << "Waiting for view_ref";
    RunLoopUntil([&] { return view_ref_from_scene.has_value(); });

    // Create request pair for ChildViewWatcher protocol. We get to use this protocol as
    // a result of the CreateViewport FIDL call below, but we need to provide both
    // ends of that channel, hand one side (server end) to Flatland, and keep the other
    // side (client end) for us.
    fidl::InterfaceHandle<fuchsia::ui::composition::ChildViewWatcher> child_view_watcher;

    // Create a viewport in this test, which will be a parent of the child view.
    // No action is committed until flatland.Present is called.
    auto view_creation_token_pair = scenic::ViewCreationTokenPair::New();
    ViewportProperties viewport_properties;
    viewport_properties.set_logical_size(fidl::Clone(layout_info.value().logical_size()));
    flatland->CreateViewport(ContentId{.value = 43},
                             std::move(view_creation_token_pair.viewport_token),
                             std::move(viewport_properties), child_view_watcher.NewRequest());
    flatland->Present(PresentArgs{});

    // Now create a viewport (parent side of the scene rendering), and let the child view
    // know how to connect its view to our viewport.
    std::optional<ChildViewStatus> child_view_status;
    // This client will catch the events related to the child view that Flatland will
    // report to us.  The client issues the appropriate hanging get requests.
    ChildViewWatcherClient child_view_watcher_client{
        std::move(child_view_watcher),
        ChildViewWatcherClient::Callbacks{
            // The closure we hand to the client here will get called when Flatland decides
            // to respond to the client's hanging get.  The only task of the closure is to
            // pull the reported `status` into a variable `child_view_status ` that the tests's
            // main program flow can use.
            .on_get_status =
                [&child_view_status](ChildViewStatus status) {
                  FX_VLOGS(1) << "ChildViewStatus received";
                  child_view_status = status;
                },
            .on_get_view_ref = [](ViewRef) {},
        },
    };

    auto flutter_app_view_provider = realm_->Connect<fuchsia::ui::app::ViewProvider>();

    CreateView2Args view2_args;
    view2_args.set_view_creation_token(std::move(view_creation_token_pair.view_token));
    flutter_app_view_provider->CreateView2(std::move(view2_args));

    // All of the above setup consists of fire-and-forget calls, so we must wait on
    // some synchronization point to allow all of them to unfold.  It seems reasonable
    // to wait on the signal that the child has
    // presented its content.
    FX_LOGS(INFO) << "Wait for the child window to render";
    RunLoopUntil([&]() {
      return child_view_status.has_value() &&
             child_view_status.value() == ChildViewStatus::CONTENT_HAS_PRESENTED;
    });
  }

  // Helper method for checking the test.mouse.ResponseListener response from the client app.
  void SetResponseExpectations(uint32_t expected_x, uint32_t expected_y,
                               zx::basic_time<ZX_CLOCK_MONOTONIC>& input_injection_time,
                               std::string component_name, bool& injection_complete) {
    response_listener()->SetRespondCallback([expected_x, expected_y, component_name,
                                             &input_injection_time, &injection_complete](
                                                test::mouse::PointerData pointer_data) {
      FX_LOGS(INFO) << "Client received tap at (" << pointer_data.local_x() << ", "
                    << pointer_data.local_y() << ").";
      FX_LOGS(INFO) << "Expected tap is at approximately (" << expected_x << ", " << expected_y
                    << ").";

      zx::duration elapsed_time =
          zx::basic_time<ZX_CLOCK_MONOTONIC>(pointer_data.time_received()) - input_injection_time;
      EXPECT_TRUE(elapsed_time.get() > 0 && elapsed_time.get() != ZX_TIME_INFINITE);
      FX_LOGS(INFO) << "Input Injection Time (ns): " << input_injection_time.get();
      FX_LOGS(INFO) << "Client Received Time (ns): " << pointer_data.time_received();
      FX_LOGS(INFO) << "Elapsed Time (ns): " << elapsed_time.to_nsecs();

      // Allow for minor rounding differences in coordinates.
      EXPECT_NEAR(pointer_data.local_x(), expected_x, 1);
      EXPECT_NEAR(pointer_data.local_y(), expected_y, 1);
      EXPECT_EQ(pointer_data.component_name(), component_name);

      injection_complete = true;
    });
  }

  void AssembleRealm(const std::vector<std::pair<ChildName, LegacyUrl>>& components,
                     const std::vector<Route>& routes) {
    // Key part of service setup: have this test component vend the
    // |ResponseListener| service in the constructed realm.
    builder()->AddLocalChild(kResponseListener, response_listener());

    // Add static test realm as a component to the realm.
    builder()->AddChild(kTestRealm, "#meta/workstation-test-realm.cm");

    // Add components specific for this test case to the realm.
    for (const auto& [name, component] : components) {
      builder()->AddLegacyChild(name, component);
    }

    // Capabilities routed from static test realm up to test driver (this component).
    builder()->AddRoute(Route{.capabilities =
                                  {
                                      Protocol{fuchsia::ui::scenic::Scenic::Name_},
                                      Protocol{fuchsia::ui::composition::Flatland::Name_},
                                      Protocol{::fuchsia::session::scene::Manager::Name_},
                                      Protocol{test::inputsynthesis::Mouse::Name_},
                                  },
                              .source = ChildRef{kTestRealm},
                              .targets = {ParentRef()}});

    // Capabilities routed from test_manager to components in static test realm.
    builder()->AddRoute(Route{.capabilities =
                                  {
                                      Protocol{fuchsia::logger::LogSink::Name_},
                                      Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                                      Protocol{fuchsia::sysmem::Allocator::Name_},
                                      Protocol{fuchsia::tracing::provider::Registry::Name_},
                                      Protocol{fuchsia::vulkan::loader::Loader::Name_},
                                  },
                              .source = ParentRef(),
                              .targets = {ChildRef{kTestRealm}}});

    // Add the necessary routing for each of the extra components added above.
    for (const auto& route : routes) {
      builder()->AddRoute(route);
    }

    // Finally, build the realm using the provided components and routes.
    realm_ = std::make_unique<RealmRoot>(builder()->Build());
  }

  std::unique_ptr<RealmBuilder> realm_builder_;
  std::unique_ptr<RealmRoot> realm_;
  std::unique_ptr<ViewProviderServer> view_provider_server_;
  std::unique_ptr<ResponseListenerServer> response_listener_;
};

class FlutterInputTest : public MouseInputBase {
 protected:
  std::vector<std::pair<ChildName, LegacyUrl>> GetTestComponents() override {
    return {
        std::make_pair(kMouseInputFlutter, kMouseInputFlutterUrl),
    };
  }

  std::vector<Route> GetTestRoutes() override {
    return merge({GetFlutterRoutes(ChildRef{kMouseInputFlutter}),
                  {
                      {.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                       .source = ChildRef{kMouseInputFlutter},
                       .targets = {ParentRef()}},
                  }});
  }

  // Routes needed to setup Flutter client.
  static std::vector<Route> GetFlutterRoutes(ChildRef target) {
    return {{.capabilities =
                 {
                     Protocol{test::mouse::ResponseListener::Name_},
                 },
             .source = ChildRef{kResponseListener},
             .targets = {target}},
            {.capabilities =
                 {
                     Protocol{fuchsia::cobalt::LoggerFactory::Name_},
                     Protocol{fuchsia::ui::composition::Allocator::Name_},
                     Protocol{fuchsia::ui::composition::Flatland::Name_},
                     Protocol{fuchsia::ui::scenic::Scenic::Name_},
                 },
             .source = ChildRef{kTestRealm},
             .targets = {target}},
            {.capabilities =
                 {
                     // Redirect logging output for the test realm to
                     // the host console output.
                     Protocol{fuchsia::logger::LogSink::Name_},
                     Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                     Protocol{fuchsia::sysmem::Allocator::Name_},
                     Protocol{fuchsia::tracing::provider::Registry::Name_},
                     Protocol{fuchsia::vulkan::loader::Loader::Name_},
                 },
             .source = ParentRef(),
             .targets = {target}}};
  }

  static constexpr auto kMouseInputFlutter = "mouse-input-flutter";
  static constexpr auto kMouseInputFlutterUrl =
      "fuchsia-pkg://fuchsia.com/mouse-input-test#meta/mouse-input-flutter.cmx";
};

#ifndef INPUT_USE_MODERN_INPUT_INJECTION
TEST_F(FlutterInputTest, DISABLED_FlutterMouseMove) {
  // Pass the test where modern injection is unavailable.  Modern injection is
  // not available outside of devices that support mouse, on which this test
  // doesn't apply anyways.
#else
TEST_F(FlutterInputTest, FlutterMouseMove) {
#endif
  // Use `ZX_CLOCK_MONOTONIC` to avoid complications due to wall-clock time changes.
  zx::basic_time<ZX_CLOCK_MONOTONIC> input_injection_time(0);

  bool initialization_complete = false;
  SetResponseExpectations(/*expected_x=*/0,
                          /*expected_y=*/0, input_injection_time,
                          /*component_name=*/"mouse-input-flutter", initialization_complete);

  LaunchClient("FlutterMouseMove");

  FX_LOGS(INFO) << "Wait for the initial mouse state";
  RunLoopUntil([&initialization_complete] { return initialization_complete; });

  // TODO: Inject input.
  auto input_synthesis = realm_->Connect<test::inputsynthesis::Mouse>();
}

}  // namespace
