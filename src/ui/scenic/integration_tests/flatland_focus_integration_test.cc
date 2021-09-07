// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/lifecycle/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_identity.h>
#include <zircon/status.h>

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/scenic/lib/utils/helpers.h"

// This test exercises the focus protocols implemented by Scenic (fuchsia.ui.focus.FocusChain,
// fuchsia.ui.views.Focuser, fuchsia.ui.views.ViewRefFocused) in the context of the Flatland
// compositor interface. The geometry is not important in this test, so we use the following
// two-node tree topology:
//    parent
//      |
//    child
namespace integration_tests {

#define EXPECT_VIEW_REF_MATCH(view_ref1, view_ref2) \
  EXPECT_EQ(utils::ExtractKoid(view_ref1), utils::ExtractKoid(view_ref2))

using fuchsia::ui::composition::ChildViewWatcher;
using fuchsia::ui::composition::ContentId;
using fuchsia::ui::composition::Flatland;
using fuchsia::ui::composition::FlatlandDisplay;
using fuchsia::ui::composition::ParentViewportWatcher;
using fuchsia::ui::composition::TransformId;
using fuchsia::ui::composition::ViewBoundProtocols;
using fuchsia::ui::composition::ViewportProperties;
using fuchsia::ui::focus::FocusChain;
using fuchsia::ui::focus::FocusChainListener;
using fuchsia::ui::views::ViewCreationToken;
using fuchsia::ui::views::ViewportCreationToken;
using fuchsia::ui::views::ViewRef;

namespace {

const std::map<std::string, std::string> LocalServices() {
  return {{"fuchsia.ui.composition.Allocator",
           "fuchsia-pkg://fuchsia.com/flatland_integration_tests#meta/scenic.cmx"},
          {"fuchsia.ui.composition.Flatland",
           "fuchsia-pkg://fuchsia.com/flatland_integration_tests#meta/scenic.cmx"},
          {"fuchsia.ui.composition.FlatlandDisplay",
           "fuchsia-pkg://fuchsia.com/flatland_integration_tests#meta/scenic.cmx"},
          {"fuchsia.ui.focus.FocusChainListenerRegistry",
           "fuchsia-pkg://fuchsia.com/flatland_integration_tests#meta/scenic.cmx"},
          // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
          {"fuchsia.ui.lifecycle.LifecycleController",
           "fuchsia-pkg://fuchsia.com/flatland_integration_tests#meta/scenic.cmx"},
          {"fuchsia.hardware.display.Provider",
           "fuchsia-pkg://fuchsia.com/fake-hardware-display-controller-provider#meta/hdcp.cmx"}};
}

// Allow these global services.
const std::vector<std::string> GlobalServices() {
  return {"fuchsia.vulkan.loader.Loader", "fuchsia.sysmem.Allocator"};
}

// "Long enough" time to wait before assuming updates won't arrive.
// Should not be used when actually expecting an update to occur.
const zx::duration kWaitTime = zx::msec(100);
const uint32_t kDefaultLogicalPixelSize = 1;

std::pair<ViewCreationToken, ViewportCreationToken> NewViewCreationTokens() {
  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  {
    const auto status = zx::channel::create(0, &parent_token.value, &child_token.value);
    FX_CHECK(status == ZX_OK);
  }
  return std::make_pair<ViewCreationToken, ViewportCreationToken>(std::move(child_token),
                                                                  std::move(parent_token));
}

}  // namespace

class FlatlandFocusIntegrationTest : public gtest::TestWithEnvironmentFixture,
                                     public FocusChainListener {
 protected:
  FlatlandFocusIntegrationTest() : focus_chain_listener_(this){};

  void SetUp() override {
    TestWithEnvironmentFixture::SetUp();
    environment_ = CreateNewEnclosingEnvironment("flatland_touch_integration_test_environment",
                                                 CreateServices());
    WaitForEnclosingEnvToStart(environment_.get());

    // Connects to scenic lifecycle controller in order to shutdown scenic at the end of the test.
    // This ensures the correct ordering of shutdown under CFv1: first scenic, then the fake display
    // controller.
    //
    // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
    environment_->ConnectToService<fuchsia::ui::lifecycle::LifecycleController>(
        scenic_lifecycle_controller_.NewRequest());

    // Set up focus chain listener and wait for the initial null focus chain.
    fidl::InterfaceHandle<FocusChainListener> listener_handle;
    focus_chain_listener_.Bind(listener_handle.NewRequest());
    environment_->ConnectToService<fuchsia::ui::focus::FocusChainListenerRegistry>()->Register(
        std::move(listener_handle));
    EXPECT_EQ(CountReceivedFocusChains(), 0u);
    RunLoopUntil([this] { return CountReceivedFocusChains() == 1u; });
    EXPECT_FALSE(LastFocusChain()->has_focus_chain());

    // Set up the display.
    environment_->ConnectToService(flatland_display_.NewRequest());
    flatland_display_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
    });
    fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
    auto [child_token, parent_token] = NewViewCreationTokens();
    flatland_display_->SetContent(std::move(parent_token), child_view_watcher.NewRequest());

    // Set up root view.
    environment_->ConnectToService(root_session_.NewRequest());
    root_session_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
    });
    fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    root_view_ref_ = fidl::Clone(identity.view_ref);
    ViewBoundProtocols protocols;
    protocols.set_view_focuser(root_focuser_.NewRequest());
    root_session_->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                               parent_viewport_watcher.NewRequest());
    BlockingPresent(root_session_);

    // Now that the scene exists, wait for a valid focus chain. It should only contain the root
    // view.
    RunLoopUntil([this] { return CountReceivedFocusChains() == 2u; });
    EXPECT_TRUE(LastFocusChain()->has_focus_chain());
    ASSERT_EQ(LastFocusChain()->focus_chain().size(), 1u);
    EXPECT_VIEW_REF_MATCH(LastFocusChain()->focus_chain().front(), root_view_ref_);

    observed_focus_chains_.clear();
  }

  void TearDown() override {
    // Avoid spurious errors since we are about to kill scenic.
    //
    // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
    flatland_display_.set_error_handler(nullptr);
    focus_chain_listener_.set_error_handler(nullptr);
    root_focuser_.set_error_handler(nullptr);
    root_session_.set_error_handler(nullptr);

    zx_status_t terminate_status = scenic_lifecycle_controller_->Terminate();
    FX_CHECK(terminate_status == ZX_OK)
        << "Failed to terminate Scenic with status: " << zx_status_get_string(terminate_status);
  }

  // Configures services available to the test environment. This method is called by |SetUp()|. It
  // shadows but calls |TestWithEnvironmentFixture::CreateServices()|.
  std::unique_ptr<sys::testing::EnvironmentServices> CreateServices() {
    auto services = TestWithEnvironmentFixture::CreateServices();
    for (const auto& [name, url] : LocalServices()) {
      const zx_status_t is_ok = services->AddServiceWithLaunchInfo({.url = url}, name);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << name;
    }

    for (const auto& service : GlobalServices()) {
      const zx_status_t is_ok = services->AllowParentService(service);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << service;
    }

    return services;
  }

  void BlockingPresent(fuchsia::ui::composition::FlatlandPtr& flatland) {
    bool presented = false;
    flatland.events().OnFramePresented = [&presented](auto) { presented = true; };
    flatland->Present({});
    RunLoopUntil([&presented] { return presented; });
    flatland.events().OnFramePresented = nullptr;
  }

  bool RequestFocusChange(fuchsia::ui::views::FocuserPtr& view_focuser_ptr, const ViewRef& target) {
    FX_CHECK(view_focuser_ptr.is_bound());
    bool request_processed = false;
    bool request_honored = false;
    view_focuser_ptr->RequestFocus(fidl::Clone(target),
                                   [&request_processed, &request_honored](auto result) {
                                     request_processed = true;
                                     if (!result.is_err()) {
                                       request_honored = true;
                                     }
                                   });
    RunLoopUntil([&request_processed] { return request_processed; });
    return request_honored;
  }

  void AttachToRoot(ViewportCreationToken&& token) {
    fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
    ViewportProperties properties;
    properties.set_logical_size({kDefaultLogicalPixelSize, kDefaultLogicalPixelSize});
    const TransformId kRootTransform{.value = 1};
    const ContentId kRootContent{.value = 1};
    root_session_->CreateTransform(kRootTransform);
    root_session_->CreateViewport(kRootContent, std::move(token), std::move(properties),
                                  child_view_watcher.NewRequest());
    root_session_->SetRootTransform(kRootTransform);
    root_session_->SetContent(kRootTransform, kRootContent);
    BlockingPresent(root_session_);
  }

  // |fuchsia::ui::focus::FocusChainListener|
  void OnFocusChange(FocusChain focus_chain, OnFocusChangeCallback callback) override {
    observed_focus_chains_.push_back(std::move(focus_chain));
    callback();  // Receipt.
  }

  size_t CountReceivedFocusChains() const { return observed_focus_chains_.size(); }

  const FocusChain* LastFocusChain() const {
    if (observed_focus_chains_.empty()) {
      return nullptr;
    } else {
      // Can't do std::optional<const FocusChain&>.
      return &observed_focus_chains_.back();
    }
  }

  fuchsia::ui::composition::FlatlandPtr root_session_;
  fuchsia::ui::views::ViewRef root_view_ref_;
  fuchsia::ui::views::FocuserPtr root_focuser_;

  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;

 private:
  fidl::Binding<FocusChainListener> focus_chain_listener_;
  std::vector<FocusChain> observed_focus_chains_;

  fuchsia::ui::lifecycle::LifecycleControllerSyncPtr scenic_lifecycle_controller_;
  fuchsia::ui::composition::FlatlandDisplayPtr flatland_display_;
};

TEST_F(FlatlandFocusIntegrationTest, RequestValidity_RequestUnconnected_ShouldFail) {
  EXPECT_EQ(CountReceivedFocusChains(), 0u);

  // Set up the child view.
  auto [child_token, parent_token] = NewViewCreationTokens();
  fuchsia::ui::composition::FlatlandPtr child_session;
  environment_->ConnectToService(child_session.NewRequest());
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  auto identity = scenic::NewViewIdentityOnCreation();
  auto child_view_ref = fidl::Clone(identity.view_ref);
  child_session->CreateView2(std::move(child_token), std::move(identity), {},
                             parent_viewport_watcher.NewRequest());
  BlockingPresent(child_session);

  // Not connected yet, so focus change requests should fail.
  EXPECT_FALSE(RequestFocusChange(root_focuser_, child_view_ref));
  RunLoopWithTimeout(kWaitTime);
  EXPECT_EQ(CountReceivedFocusChains(), 0u);
}

TEST_F(FlatlandFocusIntegrationTest, RequestValidity_RequestConnected_ShouldSucceed) {
  // Set up the child view.
  auto [child_token, parent_token] = NewViewCreationTokens();
  fuchsia::ui::composition::FlatlandPtr child_session;
  environment_->ConnectToService(child_session.NewRequest());
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  auto identity = scenic::NewViewIdentityOnCreation();
  auto child_view_ref = fidl::Clone(identity.view_ref);
  child_session->CreateView2(std::move(child_token), std::move(identity), {},
                             parent_viewport_watcher.NewRequest());
  BlockingPresent(child_session);

  // Attach to root.
  AttachToRoot(std::move(parent_token));

  EXPECT_EQ(CountReceivedFocusChains(), 0u);
  // Move focus from the root to the child view.
  EXPECT_TRUE(RequestFocusChange(root_focuser_, child_view_ref));
  RunLoopUntil([this] { return CountReceivedFocusChains() == 1; });
  // FocusChain should contain root view + child view.
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 2u);
  EXPECT_VIEW_REF_MATCH(LastFocusChain()->focus_chain()[0], root_view_ref_);
  EXPECT_VIEW_REF_MATCH(LastFocusChain()->focus_chain()[1], child_view_ref);
}

TEST_F(FlatlandFocusIntegrationTest, RequestValidity_SelfRequest_ShouldSucceed) {
  // Set up the child view and attach it to the root.
  auto [child_token, parent_token] = NewViewCreationTokens();
  AttachToRoot(std::move(parent_token));

  fuchsia::ui::composition::FlatlandPtr child_session;
  environment_->ConnectToService(child_session.NewRequest());
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  fuchsia::ui::views::FocuserPtr child_focuser;
  ViewBoundProtocols protocols;
  protocols.set_view_focuser(child_focuser.NewRequest());
  auto identity = scenic::NewViewIdentityOnCreation();
  auto child_view_ref = fidl::Clone(identity.view_ref);
  child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                             parent_viewport_watcher.NewRequest());
  BlockingPresent(child_session);

  // Child is not focused. Trying to move focus at this point should fail.
  EXPECT_FALSE(RequestFocusChange(child_focuser, child_view_ref));
  EXPECT_EQ(CountReceivedFocusChains(), 0u);
  // First move focus from the root view to the child view.
  EXPECT_TRUE(RequestFocusChange(root_focuser_, child_view_ref));
  // Then move focus from the child view to itself. Should now succeed.
  EXPECT_TRUE(RequestFocusChange(child_focuser, child_view_ref));
  // Should only receive one focus chain, since it didn't change from the second request.
  RunLoopUntil([this] { return CountReceivedFocusChains() == 1; });
  RunLoopWithTimeout(kWaitTime);
  EXPECT_EQ(CountReceivedFocusChains(), 1u);
  // Should contain root view + child view.
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 2u);
  EXPECT_VIEW_REF_MATCH(LastFocusChain()->focus_chain()[0], root_view_ref_);
  EXPECT_VIEW_REF_MATCH(LastFocusChain()->focus_chain()[1], child_view_ref);
}

TEST_F(FlatlandFocusIntegrationTest, ChildView_CreatedBeforeAttachingToRoot_ShouldNotKillFocuser) {
  // Set up the child view.
  auto [child_token, parent_token] = NewViewCreationTokens();
  fuchsia::ui::composition::FlatlandPtr child_session;
  environment_->ConnectToService(child_session.NewRequest());
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  fuchsia::ui::views::FocuserPtr child_focuser;
  bool channel_alive = true;
  child_focuser.set_error_handler([&channel_alive](auto) { channel_alive = false; });

  ViewBoundProtocols protocols;
  protocols.set_view_focuser(child_focuser.NewRequest());
  auto identity = scenic::NewViewIdentityOnCreation();
  auto child_view_ref = fidl::Clone(identity.view_ref);
  child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                             parent_viewport_watcher.NewRequest());
  BlockingPresent(child_session);

  // Attach to root.
  AttachToRoot(std::move(parent_token));

  // The child_focuser should not die.
  RunLoopUntilIdle();
  EXPECT_TRUE(channel_alive);
}

TEST_F(FlatlandFocusIntegrationTest, FocusChain_Updated_OnViewDisconnect) {
  // Set up the child view.
  auto [child_token, parent_token] = NewViewCreationTokens();
  fuchsia::ui::composition::FlatlandPtr child_session;
  environment_->ConnectToService(child_session.NewRequest());
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  auto identity = scenic::NewViewIdentityOnCreation();
  auto child_view_ref = fidl::Clone(identity.view_ref);
  child_session->CreateView2(std::move(child_token), std::move(identity), {},
                             parent_viewport_watcher.NewRequest());
  BlockingPresent(child_session);

  // Attach to root.
  AttachToRoot(std::move(parent_token));

  EXPECT_EQ(CountReceivedFocusChains(), 0u);
  // Try to move focus to child. Should succeed.
  EXPECT_TRUE(RequestFocusChange(root_focuser_, child_view_ref));
  RunLoopUntil([this] { return CountReceivedFocusChains() == 1u; });  // Succeeds or times out.
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 2u);

  // Disconnect the child and watch the focus chain update.
  const ContentId kRootContent{.value = 1};
  root_session_->ReleaseViewport(kRootContent, [](auto) {});
  BlockingPresent(root_session_);
  RunLoopUntil([this] { return CountReceivedFocusChains() == 2u; });  // Succeeds or times out.
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 1u);
  EXPECT_VIEW_REF_MATCH(LastFocusChain()->focus_chain()[0], root_view_ref_);
}

TEST_F(FlatlandFocusIntegrationTest, ViewFocuserDisconnectDoesNotKillSession) {
  root_session_.set_error_handler([](zx_status_t) { FAIL() << "Client shut down unexpectedly."; });
  root_focuser_.Unbind();
  // Wait "long enough" and observe that the session channel doesn't close.
  RunLoopWithTimeout(kWaitTime);
}

TEST_F(FlatlandFocusIntegrationTest, ViewRefFocused_HappyCase) {
  // Set up the child view.
  auto [child_token, parent_token] = NewViewCreationTokens();
  AttachToRoot(std::move(parent_token));
  fuchsia::ui::composition::FlatlandPtr child_session;
  environment_->ConnectToService(child_session.NewRequest());
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  fuchsia::ui::views::ViewRefFocusedPtr child_focused_ptr;
  ViewBoundProtocols protocols;
  protocols.set_view_ref_focused(child_focused_ptr.NewRequest());
  auto identity = scenic::NewViewIdentityOnCreation();
  auto child_view_ref = fidl::Clone(identity.view_ref);
  child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                             parent_viewport_watcher.NewRequest());
  BlockingPresent(child_session);

  // Watch for child focused event.
  std::optional<bool> child_focused;
  child_focused_ptr->Watch([&child_focused](auto update) {
    ASSERT_TRUE(update.has_focused());
    child_focused = update.focused();
  });
  RunLoopUntilIdle();
  EXPECT_FALSE(child_focused.has_value());

  // Focus the child and confirm the event arriving.
  EXPECT_TRUE(RequestFocusChange(root_focuser_, child_view_ref));
  RunLoopUntil([&child_focused] { return child_focused.has_value(); });
  EXPECT_TRUE(child_focused.value());
  EXPECT_TRUE(child_focused_ptr.is_bound());
}

#undef EXPECT_VIEW_REF_MATCH

}  // namespace integration_tests
