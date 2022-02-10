// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/fonts/cpp/fidl.h>
#include <fuchsia/hardware/display/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/kernel/cpp/fidl.h>
#include <fuchsia/memorypressure/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <fuchsia/posix/socket/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/session/scene/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <fuchsia/web/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/component_context.h>
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
#include <src/lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>
#include <test/inputsynthesis/cpp/fidl.h>
#include <test/text/cpp/fidl.h>

#include "fuchsia/sysmem/cpp/fidl.h"

namespace {

using fuchsia::io::Operations;
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
using sys::testing::Directory;
using sys::testing::LocalComponent;
using sys::testing::LocalComponentHandles;
using sys::testing::ParentRef;
using sys::testing::Protocol;
using sys::testing::Route;
using sys::testing::experimental::RealmRoot;
using RealmBuilder = sys::testing::experimental::RealmBuilder;

// Max timeout in failure cases.
// Set this as low as you can that still works across all test platforms.
constexpr zx::duration kTimeout = zx::min(5);

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
  void Start(std::unique_ptr<LocalComponentHandles> local_handles) override {
    // When this component starts, add a binding to the test.touch.ResponseListener
    // protocol to this component's outgoing directory.
    FX_CHECK(local_handles->outgoing()->AddPublicService(
                 fidl::InterfaceRequestHandler<ViewProvider>([this](auto request) {
                   bindings_.AddBinding(this, std::move(request), dispatcher_);
                 })) == ZX_OK);
    local_handles_.emplace_back(std::move(local_handles));
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
  std::vector<std::unique_ptr<LocalComponentHandles>> local_handles_;
  fidl::BindingSet<ViewProvider> bindings_;

  std::function<void(CreateView2Args)> create_view2_callback_ = nullptr;
};

// `ResponseListener` is a local test protocol that our test Flutter app uses to let us know
// what text is being entered into its only text field.
//
// The text field contents are reported on almost every change, so if you are entering a long
// text, you will see calls corresponding to successive additions of characters, not just the
// end result.
class TestResponseListenerServer : public test::text::ResponseListener, public LocalComponent {
 public:
  explicit TestResponseListenerServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  TestResponseListenerServer(const TestResponseListenerServer&) = delete;
  TestResponseListenerServer& operator=(const TestResponseListenerServer&) = delete;

  // `test.text.ResponseListener/Respond`.
  void Respond(test::text::Response response,
               test::text::ResponseListener::RespondCallback callback) override {
    FX_VLOGS(1) << "Flutter app sent: '" << response.text() << "'";
    response_ = response.text();
    callback();
  }

  // Starts this server.
  void Start(std::unique_ptr<LocalComponentHandles> handles) override {
    handles_ = std::move(handles);

    ASSERT_EQ(ZX_OK,
              handles_->outgoing()->AddPublicService(bindings_.GetHandler(this, dispatcher_)));
  }

  // Returns true if the last response received matches `expected`.  If a match is found,
  // the match is consumed, so a next call to HasResponse starts from scratch.
  bool HasResponse(const std::string& expected) {
    bool match = response_.has_value() && response_.value() == expected;
    if (match) {
      response_ = std::nullopt;
    }
    return match;
  }

 private:
  // Not owned.
  async_dispatcher_t* dispatcher_ = nullptr;
  fidl::BindingSet<test::text::ResponseListener> bindings_;
  std::unique_ptr<LocalComponentHandles> handles_;
  std::optional<std::string> response_;
};

constexpr auto kTestRealm = "workstation-test-realm";
constexpr auto kTextInputFlutter = "text_input_flutter";
constexpr auto kResponseListener = "test_text_response_listener";

class TextInputTest : public gtest::RealLoopFixture {
 protected:
  TextInputTest()
      : realm_builder_(std::make_unique<RealmBuilder>(RealmBuilder::Create())),
        view_provider_server_(std::make_unique<ViewProviderServer>(dispatcher())),
        test_response_listener_(std::make_unique<TestResponseListenerServer>(dispatcher())) {}

  void SetUp() override {
    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTimeout);

    SetUpRealm(realm_builder_.get());

    realm_ = std::make_unique<RealmRoot>(realm_builder_->Build());
  }

  void SetUpRealm(RealmBuilder* builder) {
    builder->AddLocalChild(kResponseListener, test_response_listener_.get());

    builder->AddChild(kTestRealm, "#meta/workstation-test-realm.cm");

    // Capabilities offered to this test fixture by the test realm.
    builder->AddRoute(Route{.capabilities =
                                {
                                    Protocol{fuchsia::ui::scenic::Scenic::Name_},
                                    Protocol{fuchsia::ui::composition::Flatland::Name_},
                                    Protocol{::fuchsia::session::scene::Manager::Name_},
                                    Protocol{test::inputsynthesis::Text::Name_},
                                },
                            .source = ChildRef{kTestRealm},
                            .targets = {ParentRef()}});

    builder->AddLegacyChild(
        kTextInputFlutter, "fuchsia-pkg://fuchsia.com/text-input-test#meta/text-input-flutter.cmx");

    // Capabilities given to this test fixture by the test flutter app.
    builder->AddRoute(Route{.capabilities =
                                {
                                    Protocol{fuchsia::ui::app::ViewProvider::Name_},
                                },
                            .source = ChildRef{kTextInputFlutter},
                            .targets = {ParentRef()}});

    // Capabilities passed down from the parent.
    builder->AddRoute(Route{.capabilities =
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
                            .targets = {ChildRef{kTestRealm}, ChildRef{kTextInputFlutter}}});
    // Capabilities given to the test app by the test realm.
    builder->AddRoute(Route{.capabilities =
                                {
                                    Protocol{fuchsia::ui::composition::Flatland::Name_},
                                    Protocol{fuchsia::ui::composition::Allocator::Name_},
                                    Protocol{fuchsia::ui::input::ImeService::Name_},
                                    Protocol{fuchsia::ui::input3::Keyboard::Name_},
                                    Protocol{fuchsia::cobalt::LoggerFactory::Name_},
                                    Protocol{fuchsia::ui::scenic::Scenic::Name_},
                                },
                            .source = ChildRef{kTestRealm},
                            .targets = {ChildRef{kTextInputFlutter}}});

    // Test-specific instrumentation.
    builder->AddRoute(Route{.capabilities =
                                {
                                    Protocol{test::text::ResponseListener::Name_},
                                },
                            .source = ChildRef{kResponseListener},
                            .targets = {ChildRef{kTextInputFlutter}}});
  }

  std::unique_ptr<RealmBuilder> realm_builder_;
  std::unique_ptr<RealmRoot> realm_;
  std::unique_ptr<ViewProviderServer> view_provider_server_;
  std::unique_ptr<TestResponseListenerServer> test_response_listener_;
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

#ifndef INPUT_USE_MODERN_INPUT_INJECTION
TEST_F(TextInputTest, DISABLED_FlutterTextFieldEntry) {
  // Pass the test where modern injection is unavailable.  Modern injection is
  // not available outside of devices that support keyboard, on which this test
  // doesn't apply anyways.
#else
TEST_F(TextInputTest, FlutterTextFieldEntry) {
#endif
  auto scene_manager = realm_->Connect<fuchsia::session::scene::Manager>();

  fidl::InterfaceHandle<ViewProvider> view_provider_handle;
  view_provider_server_->Bind(view_provider_handle.NewRequest());

  std::optional<fuchsia::ui::app::CreateView2Args> args;
  view_provider_server_->SetCreateView2Callback(
      [&](fuchsia::ui::app::CreateView2Args a) { args = std::move(a); });

  std::optional<ViewRef> view_ref_from_scene;
  scene_manager->SetRootView(std::move(view_provider_handle), [&view_ref_from_scene](ViewRef ref) {
    view_ref_from_scene = std::move(ref);
  });
  FX_LOGS(INFO) << "Waiting for args";
  RunLoopUntil([&] { return args.has_value(); });

  // Must connect to the scene graph here.

  auto flatland = realm_->Connect<fuchsia::ui::composition::Flatland>();
  flatland.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "flatland error status: " << zx_status_get_string(status);
  });
  flatland->SetDebugName("text-input-test");

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

  // A call to flatland.Present commits all previously scheduled operations.
  flatland->Present(PresentArgs{});

  FX_LOGS(INFO) << "Waiting for status info.";
  RunLoopUntil([&] {
    return status_info.has_value() &&
           status_info.value() == ParentViewportStatus::CONNECTED_TO_DISPLAY;
  });

  // I suppose when this happens then our view has been presented.
  FX_LOGS(INFO) << "Waiting for view_ref";
  RunLoopUntil([&] { return view_ref_from_scene.has_value(); });  // .await

  // Now, on to installing a view from flutter.  Flutter's view must be a child of
  // the viewport that this test fixture creates.

  // Create request pair for ChildViewWatcher protocol.  We get to use this protocol as
  // a result of the CreateViewport FIDL call below, but we need to provide both
  // ends of that channel, hand one side (server end) to Flatland, and keep the other
  // side (client end) for us.
  fidl::InterfaceHandle<fuchsia::ui::composition::ChildViewWatcher> child_view_watcher;

  // Create a viewport in this test, which will be a parent of the flutter app view.
  // No action is committed until flatland.Present is called.
  auto view_creation_token_pair = scenic::ViewCreationTokenPair::New();
  ViewportProperties viewport_properties;
  viewport_properties.set_logical_size(fidl::Clone(layout_info.value().logical_size()));
  flatland->CreateViewport(ContentId{.value = 43},
                           std::move(view_creation_token_pair.viewport_token),
                           std::move(viewport_properties), child_view_watcher.NewRequest());
  flatland->Present(PresentArgs{});

  // Now create a viewport (parent side of the scene rendering), and let the flutter app
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
  // This is hopefully enough for Flutter to start.
  flutter_app_view_provider->CreateView2(std::move(view2_args));

  // All of the above setup consists of fire-and-forget calls, so we must wait on
  // some synchronization point to allow all of them to unfold.  It seems reasonable
  // to wait on the signal that the child (i.e. in this case the flutter app) has
  // presented its content.

  FX_LOGS(INFO) << "Wait for the child window to render";
  RunLoopUntil([&]() {
    return child_view_status.has_value() &&
           child_view_status.value() == ChildViewStatus::CONTENT_HAS_PRESENTED;
  });

  FX_LOGS(INFO) << "Wait for the initial text response";
  RunLoopUntil([&] { return test_response_listener_->HasResponse(""); });

  // If the child has rendered, this means the flutter app is alive. Yay!
  //
  // Now, send it some text. test_response_listener_ will eventually contain
  // the entire response.

  auto input_synthesis = realm_->Connect<test::inputsynthesis::Text>();

  FX_LOGS(INFO) << "Sending a text message";
  bool done = false;
  input_synthesis->Send("Hello world!", [&done]() { done = true; });
  RunLoopUntil([&] { return done; });

  FX_LOGS(INFO) << "Message was sent";

  // Sadly, we can only wait until test timeout if this fails.
  RunLoopUntil([&] { return test_response_listener_->HasResponse("Hello world!"); });

  FX_LOGS(INFO) << "Done";
}

}  // namespace
