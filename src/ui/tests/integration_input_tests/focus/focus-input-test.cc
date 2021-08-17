// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/lifecycle/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
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

// Max timeout in failure cases.
// Set this as low as you can that still works across all test platforms.
constexpr zx::duration kTimeout = zx::min(5);

// Common services for each test.
const std::map<std::string, std::string> LocalServices() {
  return {
      // Root Presenter is bundled with the test package to ensure version hermeticity and driver
      // hermeticity.
      {"fuchsia.ui.policy.Presenter",
       "fuchsia-pkg://fuchsia.com/focus-input-test#meta/root_presenter.cmx"},
      // Scenic protocols.
      {"fuchsia.ui.scenic.Scenic", "fuchsia-pkg://fuchsia.com/focus-input-test#meta/scenic.cmx"},
      {"fuchsia.ui.focus.FocusChainListenerRegistry",
       "fuchsia-pkg://fuchsia.com/focus-input-test#meta/scenic.cmx"},
      // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
      {"fuchsia.ui.lifecycle.LifecycleController",
       "fuchsia-pkg://fuchsia.com/focus-input-test#meta/scenic.cmx"},
      // Misc protocols.
      {"fuchsia.cobalt.LoggerFactory",
       "fuchsia-pkg://fuchsia.com/mock_cobalt#meta/mock_cobalt.cmx"},
      {"fuchsia.hardware.display.Provider",
       "fuchsia-pkg://fuchsia.com/fake-hardware-display-controller-provider#meta/hdcp.cmx"},
  };
}

// Allow these global services from outside the test environment.
const std::vector<std::string> GlobalServices() {
  return {"fuchsia.vulkan.loader.Loader", "fuchsia.sysmem.Allocator",
          "fuchsia.scheduler.ProfileProvider"};
}

class FocusInputTest : public gtest::TestWithEnvironmentFixture,
                       public test::focus::ResponseListener {
 protected:
  FocusInputTest() : response_listener_(this) {
    auto services = TestWithEnvironmentFixture::CreateServices();

    // Key part of service setup: have this test component vend the |ResponseListener| service to
    // the constructed environment.
    {
      zx_status_t is_ok = services->AddService<test::focus::ResponseListener>(
          [this](fidl::InterfaceRequest<test::focus::ResponseListener> request) {
            response_listener_.Bind(std::move(request));
          });
      FX_CHECK(is_ok == ZX_OK);
    }

    // Add common services.
    for (const auto& [name, url] : LocalServices()) {
      const zx_status_t is_ok = services->AddServiceWithLaunchInfo({.url = url}, name);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << name;
    }

    // Enable services from outside this test.
    for (const auto& service : GlobalServices()) {
      const zx_status_t is_ok = services->AllowParentService(service);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << service;
    }

    test_env_ = CreateNewEnclosingEnvironment("focus_input_test_env", std::move(services));
    WaitForEnclosingEnvToStart(test_env_.get());

    FX_VLOGS(1) << "Created test environment.";

    // Connects to scenic lifecycle controller in order to shutdown scenic at the end of the test.
    // This ensures the correct ordering of shutdown under CFv1: first scenic, then the fake display
    // controller.
    //
    // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
    test_env_->ConnectToService<fuchsia::ui::lifecycle::LifecycleController>(
        scenic_lifecycle_controller_.NewRequest());

    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTimeout);
  }

  ~FocusInputTest() override {
    zx_status_t terminate_status = scenic_lifecycle_controller_->Terminate();
    FX_CHECK(terminate_status == ZX_OK)
        << "Failed to terminate Scenic with status: " << zx_status_get_string(terminate_status);
  }

  void CreateScenicClientAndTestView(fuchsia::ui::views::ViewToken view_token,
                                     scenic::ViewRefPair view_ref_pair) {
    auto scenic = test_env_->ConnectToService<fuchsia::ui::scenic::Scenic>();
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

  // |test.focus.ResponseListener|
  void Respond(test::focus::Data focus_data) override {
    FX_CHECK(respond_callback_) << "Expected callback to be set for test.focus.Respond().";
    respond_callback_(std::move(focus_data));
  }

  // FIELDS

  std::unique_ptr<sys::testing::EnclosingEnvironment> test_env_;

  // Protocols used.
  fuchsia::ui::views::ViewRefFocusedPtr test_view_focus_watcher_;
  fuchsia::ui::views::FocuserPtr test_view_focuser_control_;

  // Protocols vended.
  fidl::Binding<test::focus::ResponseListener> response_listener_;

  // Scenic state.
  fuchsia::ui::lifecycle::LifecycleControllerSyncPtr scenic_lifecycle_controller_;
  std::unique_ptr<scenic::Session> session_;
  std::unique_ptr<scenic::View> test_view_;

  // Per-test action for |test.focus.ResponseListener.Respond|.
  fit::function<void(test::focus::Data)> respond_callback_;
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
  auto root_presenter = test_env_->ConnectToService<fuchsia::ui::policy::Presenter>();
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

// This test exercises the focus contract between a parent view and child view: upon focus transfer
// from parent view (this test, under Root Presenter) to child view (a simple C++ client), the
// parent view will receive a focus event with "focus=false", and the child view will receive a
// focus event with "focus=true".
TEST_F(FocusInputTest, TestView_TransfersFocus_ToChildView) {
  {  // Link test view under Root Presenter's view.
    auto tokens_rt = scenic::ViewTokenPair::New();
    auto refs_rt = scenic::ViewRefPair::New();
    fuchsia::ui::views::ViewRef test_view_ref;
    refs_rt.view_ref.Clone(&test_view_ref);

    // Instruct Root Presenter to present test view.
    auto root_presenter = test_env_->ConnectToService<fuchsia::ui::policy::Presenter>();
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
  respond_callback_ = [&child_focus_status](test::focus::Data data) {
    child_focus_status = std::move(data);
  };

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

  fuchsia::sys::ComponentControllerPtr focus_gfx_child;  // Keep child alive on stack.
  {  // Launch child component to vend child view.  Wait until child view connects.
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = "fuchsia-pkg://fuchsia.com/focus-input-test#meta/focus-gfx-client.cmx";

    // Create a point-to-point offer-use connection between parent and child.
    auto child_services = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
    focus_gfx_child = test_env_->CreateComponent(std::move(launch_info));

    auto view_provider = child_services->Connect<fuchsia::ui::app::ViewProvider>();
    view_provider->CreateViewWithViewRef(std::move(tokens_tc.view_token.value),
                                         std::move(refs_tc.control_ref),
                                         std::move(refs_tc.view_ref));
    RunLoopUntil([&child_connected] { return child_connected; });
  }

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
  using fuchsia::ui::focus::FocusChain;

  // Miniature FocusChainListener, just for this one test.
  class FocusChainListenerImpl : public fuchsia::ui::focus::FocusChainListener {
   public:
    FocusChainListenerImpl(sys::testing::EnclosingEnvironment* env,
                           std::vector<FocusChain>& collector, bool& error_fired)
        : listener_impl_(this), collector_(collector), error_fired_(error_fired) {
      env->ConnectToService(listener_registry_.NewRequest());
      listener_registry_.set_error_handler([this](zx_status_t) { error_fired_ = true; });
      listener_registry_->Register(listener_impl_.NewBinding());
    }
    // |fuchsia.ui.focus.FocusChainListener|
    void OnFocusChange(FocusChain focus_chain, OnFocusChangeCallback callback) {
      collector_.push_back(std::move(focus_chain));
      callback();
    }

   private:
    fuchsia::ui::focus::FocusChainListenerRegistryPtr listener_registry_;
    fidl::Binding<fuchsia::ui::focus::FocusChainListener> listener_impl_;
    std::vector<FocusChain>& collector_;
    bool& error_fired_;
  };

  std::vector<FocusChain> collected_a;
  bool error_fired_a = false;
  FocusChainListenerImpl listener_a(test_env_.get(), collected_a, error_fired_a);

  std::vector<FocusChain> collected_b;
  bool error_fired_b = false;
  FocusChainListenerImpl listener_b(test_env_.get(), collected_b, error_fired_b);

  RunLoopUntil([&error_fired_a, &error_fired_b, &collected_a, &collected_b] {
    // Wait until an error fired, or both listeners see their first report.
    return error_fired_a || error_fired_b || (collected_a.size() > 0 && collected_b.size() > 0);
  });

  // Client "a" is clean, and collected a focus chain.
  EXPECT_FALSE(error_fired_a);
  ASSERT_EQ(collected_a.size(), 1u);
  // It's empty, since there's no scene at time of connection.
  EXPECT_FALSE(collected_a[0].has_focus_chain());

  // Client "b" is clean, and collected a focus chain.
  EXPECT_FALSE(error_fired_b);
  ASSERT_EQ(collected_b.size(), 1u);
  // It's empty, since there's no scene at time of connection.
  EXPECT_FALSE(collected_b[0].has_focus_chain());
}

}  // namespace
