// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/real_loop_fixture.h>
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
#include <zircon/status.h>

#include <optional>

#include <gtest/gtest.h>
#include <test/focus/cpp/fidl.h>

// This test exercises the client-side view-focus machinery managed by Scenic:
// - fuchsia.ui.views.Focuser (giving focus to a particular view)
// - fuchsia.ui.views.ViewRefFocused (learning when your view gained/lost focus)
// as well as the focus contract offered by Root Presenter.
//
// This test uses the following components: Root Presenter, Scenic, this test
// component itself, and a C++ GFX client.
//
// Synchronization: Underneath Root Presenter, the test component installs a test view to monitor
// the "real" child view. One test checks that Root Presenter transfers focus to the test view upon
// connection.
//
// The test waits for the child view to spin up and become connected to the view
// tree. Then, after the test view receives focus from Root Presenter, the test will transfer focus
// down to the child view. The child view will report back to the test that it received focus.

namespace {

using test::focus::ResponseListener;

// Types imported for the realm_builder library.
using component_testing::ChildRef;
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

constexpr auto kRootPresenter = "root_presenter";
constexpr auto kScenicTestRealm = "scenic-test-realm";
constexpr auto kResponseListener = "response_listener";

// Max timeout in failure cases.
// Set this as low as you can that still works across all test platforms.
constexpr zx::duration kTimeout = zx::min(5);

// This component implements the test.focus.ResponseListener protocol
// and the interface for a RealmBuilder LocalComponent. A LocalComponent
// is a component that is implemented here in the test, as opposed to elsewhere
// in the system. When it's inserted to the realm, it will act like a proper
// component. This is accomplished, in part, because the realm_builder
// library creates the necessary plumbing. It creates a manifest for the component
// and routes all capabilities to and from it.
class ResponseListenerServer : public ResponseListener, public LocalComponent {
 public:
  explicit ResponseListenerServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  // |test.focus.ResponseListener|
  void Respond(test::focus::Data focus_data) override {
    FX_CHECK(respond_callback_) << "Expected callback to be set for test.focus.Respond().";
    respond_callback_(std::move(focus_data));
  }

  // |LocalComponent::Start|
  // When the component framework requests for this component to start, this
  // method will be invoked by the realm_builder library.
  void Start(std::unique_ptr<LocalComponentHandles> local_handles) override {
    // When this component starts, add a binding to the test.focus.ResponseListener
    // protocol to this component's outgoing directory.
    FX_CHECK(local_handles->outgoing()->AddPublicService(
                 fidl::InterfaceRequestHandler<test::focus::ResponseListener>([this](auto request) {
                   bindings_.AddBinding(this, std::move(request), dispatcher_);
                 })) == ZX_OK);
    local_handles_ = std::move(local_handles);
  }

  void SetRespondCallback(fit::function<void(test::focus::Data)> callback) {
    respond_callback_ = std::move(callback);
  }

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  std::unique_ptr<LocalComponentHandles> local_handles_;
  fidl::BindingSet<test::focus::ResponseListener> bindings_;
  fit::function<void(test::focus::Data)> respond_callback_;
};

class FocusInputTest : public gtest::RealLoopFixture {
 protected:
  FocusInputTest() : realm_builder_(RealmBuilder::Create()) {}

  void SetUp() override {
    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTimeout);

    BuildRealm(this->GetTestComponents(), this->GetTestRoutes());
  }

  // Subclass should implement this method to add components to the test realm
  // next to the base ones added.
  virtual std::vector<std::pair<ChildName, LegacyUrl>> GetTestComponents() { return {}; }

  // Subclass should implement this method to add capability routes to the test
  // realm next to the base ones added.
  virtual std::vector<Route> GetTestRoutes() { return {}; }

  void CreateScenicClientAndTestView(fuchsia::ui::views::ViewToken view_token,
                                     scenic::ViewRefPair view_ref_pair) {
    auto scenic = realm()->Connect<fuchsia::ui::scenic::Scenic>();
    scenic.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
    });

    fuchsia::ui::scenic::SessionEndpoints endpoints;
    fuchsia::ui::scenic::SessionPtr client_endpoint;
    fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener_handle;
    fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener> listener_request =
        listener_handle.NewRequest();  // client side
    endpoints.set_session(client_endpoint.NewRequest())
        .set_session_listener(std::move(listener_handle))
        .set_view_ref_focused(test_view_focus_watcher_.NewRequest())
        .set_view_focuser(test_view_focuser_control_.NewRequest());
    scenic->CreateSessionT(std::move(endpoints), [] { /* don't block, feed forward */ });
    session_ =
        std::make_unique<scenic::Session>(std::move(client_endpoint), std::move(listener_request));
    session_->SetDebugName("focus-input-test");
    test_view_ = std::make_unique<scenic::View>(session_.get(), std::move(view_token),
                                                std::move(view_ref_pair.control_ref),
                                                std::move(view_ref_pair.view_ref), "test view");
    session_->Present2(/* when */ zx::clock::get_monotonic().get(), /* span */ 0,
                       [](auto) { FX_LOGS(INFO) << "test view created by Scenic."; });
  }

  RealmRoot* realm() { return realm_.get(); }
  ResponseListenerServer* response_listener() { return response_listener_.get(); }

  // Protocols used.
  fuchsia::ui::views::ViewRefFocusedPtr test_view_focus_watcher_;
  fuchsia::ui::views::FocuserPtr test_view_focuser_control_;

  // Scenic state.
  std::unique_ptr<scenic::Session> session_;
  std::unique_ptr<scenic::View> test_view_;

 private:
  void BuildRealm(const std::vector<std::pair<ChildName, LegacyUrl>>& components,
                  const std::vector<Route>& routes) {
    // Key part of service setup: have this test component vend the
    // |ResponseListener| service in the constructed realm.
    response_listener_ = std::make_unique<ResponseListenerServer>(dispatcher());
    realm_builder_.AddLocalChild(kResponseListener, response_listener());

    // Add all components shared by each test to the realm.
    realm_builder_.AddLegacyChild(
        kRootPresenter, "fuchsia-pkg://fuchsia.com/focus-input-test#meta/root_presenter.cmx");
    realm_builder_.AddChild(kScenicTestRealm,
                            "fuchsia-pkg://fuchsia.com/focus-input-test#meta/scenic-test-realm.cm");

    // Add components specific for this test case to the realm.
    for (const auto& [name, component] : components) {
      realm_builder_.AddLegacyChild(name, component);
    }

    // Add the necessary routing for each of the base components added above.
    // Capabilities routed from test_manager to components in realm.
    realm_builder_.AddRoute(
        Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_},
                               Protocol{fuchsia::vulkan::loader::Loader::Name_},
                               Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                               Protocol{fuchsia::sysmem::Allocator::Name_},
                               Protocol{fuchsia::tracing::provider::Registry::Name_}},
              .source = ParentRef(),
              .targets = {ChildRef{kScenicTestRealm}}});
    realm_builder_.AddRoute(
        Route{.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_}},
              .source = ParentRef(),
              .targets = {ChildRef{kRootPresenter}}});

    // Capabilities routed between siblings in realm.
    realm_builder_.AddRoute(
        Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_},
                               Protocol{fuchsia::ui::focus::FocusChainListenerRegistry::Name_}},
              .source = ChildRef{kScenicTestRealm},
              .targets = {ChildRef{kRootPresenter}}});

    // Capabilities routed up to test driver (this component).
    realm_builder_.AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::policy::Presenter::Name_}},
                                  .source = ChildRef{kRootPresenter},
                                  .targets = {ParentRef()}});
    realm_builder_.AddRoute(
        Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_},
                               Protocol{fuchsia::ui::focus::FocusChainListenerRegistry::Name_}},
              .source = ChildRef{kScenicTestRealm},
              .targets = {ParentRef()}});

    // Add the necessary routing for each of the extra components added above.
    for (const auto& route : routes) {
      realm_builder_.AddRoute(route);
    }

    // Finally, build the realm using the provided components and routes.
    realm_ = std::make_unique<RealmRoot>(realm_builder_.Build());
  }

  RealmBuilder realm_builder_;
  std::unique_ptr<RealmRoot> realm_;
  std::unique_ptr<ResponseListenerServer> response_listener_;
};

// This test exercises the focus contract with Root Presenter: the view offered to Root
// Presenter will have focus transferred to it. The test itself offers such a view to Root
// Presenter.
// NOTE. This test does not use test.focus.ResponseListener. There's not a client that listens to
// ViewRefFocused.
TEST_F(FocusInputTest, TestView_ReceivesFocusTransfer_FromRootPresenter) {
  auto tokens_rt = scenic::ViewTokenPair::New();  // Root Presenter -> Test
  auto refs_rt = scenic::ViewRefPair::New();
  fuchsia::ui::views::ViewRef test_view_ref;
  refs_rt.view_ref.Clone(&test_view_ref);

  // Instruct Root Presenter to present test view.
  auto root_presenter = realm()->Connect<fuchsia::ui::policy::Presenter>();
  root_presenter->PresentOrReplaceView2(std::move(tokens_rt.view_holder_token),
                                        std::move(test_view_ref),
                                        /* presentation */ nullptr);

  // Set up test view, to harvest focus signal. Root Presenter will ask Scenic to transfer focus
  // to this View's ViewRef.
  CreateScenicClientAndTestView(std::move(tokens_rt.view_token), std::move(refs_rt));

  std::optional<bool> focus_status;
  test_view_focus_watcher_->Watch(
      [&focus_status](fuchsia::ui::views::FocusState state) { focus_status = state.focused(); });

  RunLoopUntil([&focus_status] { return focus_status.has_value(); });
  ASSERT_TRUE(focus_status.value()) << "test view should initially receive focus";
  FX_LOGS(INFO) << "*** PASS ***";
}

class GfxFocusInputTest : public FocusInputTest {
 protected:
  std::vector<std::pair<ChildName, LegacyUrl>> GetTestComponents() override {
    return {std::make_pair(kFocusGfxClient, kFocusGfxClientUrl)};
  }

  std::vector<Route> GetTestRoutes() override {
    return {
        {.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
         .source = ChildRef{kFocusGfxClient},
         .targets = {ParentRef()}},
        {.capabilities = {Protocol{test::focus::ResponseListener::Name_}},
         .source = ChildRef{kResponseListener},
         .targets = {ChildRef{kFocusGfxClient}}},
        {.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
         .source = ChildRef{kScenicTestRealm},
         .targets = {ChildRef{kFocusGfxClient}}},
        {.capabilities = {Protocol{fuchsia::sys::Environment::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kFocusGfxClient}}},
    };
  }

 private:
  static constexpr auto kFocusGfxClient = "focus-gfx-client";
  static constexpr auto kFocusGfxClientUrl =
      "fuchsia-pkg://fuchsia.com/focus-input-test#meta/focus-gfx-client.cmx";
};

// This test exercises the focus contract between a parent view and child view: upon focus transfer
// from parent view (this test, under Root Presenter) to child view (a simple C++ client), the
// parent view will receive a focus event with "focus=false", and the child view will receive a
// focus event with "focus=true".
TEST_F(GfxFocusInputTest, TestView_TransfersFocus_ToChildView) {
  {  // Link test view under Root Presenter's view.
    auto tokens_rt = scenic::ViewTokenPair::New();
    auto refs_rt = scenic::ViewRefPair::New();
    fuchsia::ui::views::ViewRef test_view_ref;
    refs_rt.view_ref.Clone(&test_view_ref);

    // Instruct Root Presenter to present test view.
    auto root_presenter = realm()->Connect<fuchsia::ui::policy::Presenter>();
    root_presenter->PresentOrReplaceView2(std::move(tokens_rt.view_holder_token),
                                          std::move(test_view_ref),
                                          /* presentation */ nullptr);

    // Set up test view, to harvest focus signal. Root Presenter will ask Scenic to transfer focus
    // to test view's ViewRef.
    CreateScenicClientAndTestView(std::move(tokens_rt.view_token), std::move(refs_rt));
  }

  {  // Wait for test view to receive focus.
    std::optional<bool> focus_status;
    test_view_focus_watcher_->Watch(
        [&focus_status](fuchsia::ui::views::FocusState state) { focus_status = state.focused(); });

    RunLoopUntil([&focus_status] { return focus_status.has_value(); });
    ASSERT_TRUE(focus_status.value()) << "test view should initially receive focus";
  }

  auto tokens_tc = scenic::ViewTokenPair::New();  // connect test view to child view
  auto refs_tc = scenic::ViewRefPair::New();      // view ref for child view
  fuchsia::ui::views::ViewRef child_view_ref;
  refs_tc.view_ref.Clone(&child_view_ref);

  // Set up data collection from child view.
  std::optional<test::focus::Data> child_focus_status;
  response_listener()->SetRespondCallback(
      [&child_focus_status](test::focus::Data data) { child_focus_status = std::move(data); });

  bool child_connected = false;  // condition variable
  {  // Set up view holder for child view. Set up notification for when child view connects.
    scenic::ViewHolder view_holder_for_child(session_.get(), std::move(tokens_tc.view_holder_token),
                                             "test's view holder for gfx child");
    const uint32_t vh_id = view_holder_for_child.id();
    test_view_->AddChild(view_holder_for_child);
    session_->Present2(/* when */ zx::clock::get_monotonic().get(), /* span */ 0, [](auto) {
      FX_LOGS(INFO) << "test's viewholder for gfx child created by Scenic.";
    });

    session_->set_event_handler(
        [vh_id, &child_connected](const std::vector<fuchsia::ui::scenic::Event>& events) {
          for (const auto& event : events) {
            if (event.is_gfx() && event.gfx().is_view_connected() &&
                event.gfx().view_connected().view_holder_id == vh_id) {
              child_connected = true;
            }
          }
        });
  }

  auto view_provider = realm()->Connect<fuchsia::ui::app::ViewProvider>();
  view_provider->CreateViewWithViewRef(std::move(tokens_tc.view_token.value),
                                       std::move(refs_tc.control_ref), std::move(refs_tc.view_ref));
  RunLoopUntil([&child_connected] { return child_connected; });

  const zx::time request_time = zx::clock::get_monotonic();
  {  // Transfer focus to child view and watch for change in test view's focus status.
    test_view_focuser_control_->RequestFocus(std::move(child_view_ref),
                                             [](auto) { /* don't block, feed forward */ });
    FX_LOGS(INFO) << "Test requested focus transfer to child view at time " << request_time.get();

    std::optional<bool> focus_status;
    test_view_focus_watcher_->Watch(
        [&focus_status](fuchsia::ui::views::FocusState state) { focus_status = state.focused(); });

    RunLoopUntil([&focus_status] { return focus_status.has_value(); });
    EXPECT_FALSE(focus_status.value()) << "test view should lose focus";
  }

  {  // Wait for child view's version of focus data.
    RunLoopUntil([&child_focus_status] { return child_focus_status.has_value(); });
    FX_CHECK(child_focus_status.value().has_time_received()) << "contract with child view";
    FX_CHECK(child_focus_status.value().has_focus_status()) << "contract with child view";

    const zx::time receive_time = zx::time(child_focus_status.value().time_received());
    FX_LOGS(INFO) << "Child view received focus event at time " << receive_time.get();
    zx::duration latency = receive_time - request_time;
    FX_LOGS(INFO) << "JFYI focus latency: " << latency.to_usecs() << " us";

    ASSERT_TRUE(child_focus_status.value().focus_status()) << "child view should gain focus";
    FX_LOGS(INFO) << "*** PASS ***";
  }
}

// This test ensures that multiple clients can connect to the FocusChainListenerRegistry.
// It does not set up a scene; these "early" listeners should observe an empty focus chain.
// NOTE. This test does not use test.focus.ResponseListener. There's not a client that listens to
// ViewRefFocused.
TEST_F(FocusInputTest, SimultaneousCallsTo_FocusChainListenerRegistry) {
  // This implements the FocusChainListener class. Its purpose is to test that focus events
  // are actually sent out to the listeners.
  class FocusChainListenerImpl : public fuchsia::ui::focus::FocusChainListener {
   public:
    FocusChainListenerImpl(
        fidl::InterfaceRequest<fuchsia::ui::focus::FocusChainListener> listener_request,
        std::vector<fuchsia::ui::focus::FocusChain>& collector)
        : listener_binding_(this, std::move(listener_request)), collector_(collector) {}

   private:
    // |fuchsia.ui.focus.FocusChainListener|
    void OnFocusChange(fuchsia::ui::focus::FocusChain focus_chain,
                       OnFocusChangeCallback callback) override {
      collector_.push_back(std::move(focus_chain));
      callback();
    }

    fidl::Binding<fuchsia::ui::focus::FocusChainListener> listener_binding_;
    std::vector<fuchsia::ui::focus::FocusChain>& collector_;
  };

  // Register two Focus Chain listeners.
  std::vector<fuchsia::ui::focus::FocusChain> collected_a;
  fidl::InterfaceHandle<fuchsia::ui::focus::FocusChainListener> listener_a;
  auto listener_a_impl =
      std::make_unique<FocusChainListenerImpl>(listener_a.NewRequest(), collected_a);

  std::vector<fuchsia::ui::focus::FocusChain> collected_b;
  fidl::InterfaceHandle<fuchsia::ui::focus::FocusChainListener> listener_b;
  auto listener_b_impl =
      std::make_unique<FocusChainListenerImpl>(listener_b.NewRequest(), collected_b);

  // Connects to the listener registry and start listening.
  fuchsia::ui::focus::FocusChainListenerRegistryPtr focus_chain_listener_registry =
      realm()->Connect<fuchsia::ui::focus::FocusChainListenerRegistry>();
  focus_chain_listener_registry.set_error_handler([](zx_status_t status) {
    FX_LOGS(FATAL) << "Error from fuchsia::ui::focus::FocusChainListenerRegistry"
                   << zx_status_get_string(status);
  });
  focus_chain_listener_registry->Register(std::move(listener_a));
  focus_chain_listener_registry->Register(std::move(listener_b));

  RunLoopUntil([&collected_a, &collected_b] {
    // Wait until both listeners see their first report.
    return (collected_a.size() > 0 && collected_b.size() > 0);
  });

  // Client "a" is clean, and collected a focus chain.
  ASSERT_EQ(collected_a.size(), 1u);
  // It's empty, since there's no scene at time of connection.
  EXPECT_FALSE(collected_a[0].has_focus_chain());

  // Client "b" is clean, and collected a focus chain.
  ASSERT_EQ(collected_b.size(), 1u);
  // It's empty, since there's no scene at time of connection.
  EXPECT_FALSE(collected_b[0].has_focus_chain());
}

}  // namespace
