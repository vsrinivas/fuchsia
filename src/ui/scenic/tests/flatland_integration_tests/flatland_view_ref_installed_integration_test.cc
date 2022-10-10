// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async-loop/testing/cpp/real_loop.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/view_creation_tokens.h>
#include <lib/ui/scenic/cpp/view_identity.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <map>
#include <string>
#include <vector>

#include <zxtest/zxtest.h>

#include "src/ui/scenic/tests/utils/scenic_realm_builder.h"

// This test exercises the fuchsia.ui.views.ViewRefInstalled protocol implemented by Scenic
// in the context of the Flatland compositor interface.
namespace integration_tests {

namespace fuc = fuchsia::ui::composition;
namespace fuv = fuchsia::ui::views;

using RealmRoot = component_testing::RealmRoot;

using WatchResult = fuv::ViewRefInstalled_Watch_Result;

// Test fixture that sets up an environment with a Scenic we can connect to.
class FlatlandViewRefInstalledIntegrationTest : public zxtest::Test, public loop_fixture::RealLoop {
 protected:
  void SetUp() override {
    // Build the realm topology and route the protocols required by this test fixture from the
    // scenic subrealm.
    realm_ = std::make_unique<RealmRoot>(ScenicRealmBuilder()
                                             .AddRealmProtocol(fuc::Flatland::Name_)
                                             .AddRealmProtocol(fuc::FlatlandDisplay::Name_)
                                             .AddRealmProtocol(fuc::Allocator::Name_)
                                             .AddRealmProtocol(fuv::ViewRefInstalled::Name_)
                                             .Build());

    flatland_display_ = realm_->Connect<fuc::FlatlandDisplay>();
    flatland_display_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
    });

    // Set up root view.
    root_session_ = realm_->Connect<fuc::Flatland>();
    root_session_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
    });

    fidl::InterfacePtr<fuc::ChildViewWatcher> child_view_watcher;
    fuc::ViewBoundProtocols protocols;
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    flatland_display_->SetContent(std::move(parent_token), child_view_watcher.NewRequest());
    fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    root_session_->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                               parent_viewport_watcher.NewRequest());

    parent_viewport_watcher->GetLayout([this](auto layout_info) {
      ASSERT_TRUE(layout_info.has_logical_size());
      const auto [width, height] = layout_info.logical_size();
      display_width_ = static_cast<uint32_t>(width);
      display_height_ = static_cast<uint32_t>(height);
    });
    BlockingPresent(root_session_);

    view_ref_installed_ptr_ = realm_->Connect<fuv::ViewRefInstalled>();
    view_ref_installed_ptr_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to ViewRefInstalled: %s", zx_status_get_string(status));
    });
  }

  // Invokes Flatland.Present() and waits for a response from Scenic that the frame has been
  // presented.
  void BlockingPresent(fuc::FlatlandPtr& flatland) {
    bool presented = false;
    flatland.events().OnFramePresented = [&presented](auto) { presented = true; };
    flatland->Present({});
    RunLoopUntil([&presented] { return presented; });
    flatland.events().OnFramePresented = nullptr;
  }

  // Create a new transform and viewport, then call |BlockingPresent| to wait for it to take
  // effect. This can be called only once per Flatland instance, because it uses hard-coded IDs for
  // the transform and viewport.
  void ConnectChildView(fuc::FlatlandPtr& flatland, fuv::ViewportCreationToken&& token) {
    // Let the client_end die.
    fidl::InterfacePtr<fuc::ChildViewWatcher> child_view_watcher;
    fuc::ViewportProperties properties;
    properties.set_logical_size({display_width_, display_height_});

    fuc::TransformId kTransform{.value = 1};
    flatland->CreateTransform(kTransform);
    flatland->SetRootTransform(kTransform);

    const fuc::ContentId kContent{.value = 1};
    flatland->CreateViewport(kContent, std::move(token), std::move(properties),
                             child_view_watcher.NewRequest());
    flatland->SetContent(kTransform, kContent);

    BlockingPresent(flatland);
  }

  fuc::FlatlandPtr root_session_;
  fuv::ViewRefInstalledPtr view_ref_installed_ptr_;
  std::unique_ptr<RealmRoot> realm_;

 private:
  uint32_t display_width_ = 0;
  uint32_t display_height_ = 0;
  fuchsia::ui::scenic::ScenicPtr scenic_;
  fuc::FlatlandDisplayPtr flatland_display_;
};

TEST_F(FlatlandViewRefInstalledIntegrationTest, InvalidatedViewRef_ShouldReturnError) {
  std::optional<WatchResult> result;
  {
    auto identity = scenic::NewViewIdentityOnCreation();
    view_ref_installed_ptr_->Watch(std::move(identity.view_ref), [&result](auto watch_result) {
      result.emplace(std::move(watch_result));
    });
    RunLoopUntilIdle();
    EXPECT_FALSE(result.has_value());
  }  // |identity| goes out of scope. This will invalidate the ViewRef.

  RunLoopUntil([&result] { return result.has_value(); });  // Succeeds or times out.
  EXPECT_TRUE(result->is_err());
}

// The test exercises a two node topology:-
//  root_view
//      |
//  child_view
// |Watch()| on the child viewRef should return as soon as the child view gets connected to the root
// view.
TEST_F(FlatlandViewRefInstalledIntegrationTest, InstalledViewRef_ShouldReturnImmediately) {
  // Create the child view and connect it to the root view.
  fuc::FlatlandPtr child_session;
  child_session = realm_->Connect<fuc::Flatland>();

  auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
  fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
  fuc::ViewBoundProtocols protocols;
  auto identity = scenic::NewViewIdentityOnCreation();
  fuv::ViewRef view_ref_clone;
  fidl::Clone(identity.view_ref, &view_ref_clone);
  ConnectChildView(root_session_, std::move(parent_token));

  child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                             parent_viewport_watcher.NewRequest());

  BlockingPresent(child_session);

  std::optional<WatchResult> result;
  view_ref_installed_ptr_->Watch(std::move(view_ref_clone), [&result](auto watch_result) {
    result.emplace(std::move(watch_result));
  });

  RunLoopUntil([&result] { return result.has_value(); });  // Succeeds or times out.
  EXPECT_TRUE(result->is_response());
}

// The test exercises a two node topology:-
//  root_view
//      |
//  child_view
// |Watch()| on the child viewRef should only return when a child view gets connected to the root
// view.
TEST_F(FlatlandViewRefInstalledIntegrationTest, WaitedOnViewRef_ShouldReturnWhenInstalled) {
  auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
  auto identity = scenic::NewViewIdentityOnCreation();
  fuv::ViewRef view_ref_clone;
  fidl::Clone(identity.view_ref, &view_ref_clone);

  std::optional<WatchResult> result;
  view_ref_installed_ptr_->Watch(std::move(view_ref_clone), [&result](auto watch_result) {
    result.emplace(std::move(watch_result));
  });

  // ViewRef not installed; should not return yet.
  RunLoopUntilIdle();
  EXPECT_FALSE(result.has_value());

  // Create the child view with the viewRef.
  fuc::FlatlandPtr child_session;
  child_session = realm_->Connect<fuc::Flatland>();

  fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
  fuc::ViewBoundProtocols protocols;

  ConnectChildView(root_session_, std::move(parent_token));

  child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                             parent_viewport_watcher.NewRequest());

  BlockingPresent(child_session);

  // |Watch()| returns as the view ref is now installed.
  RunLoopUntil([&result] { return result.has_value(); });  // Succeeds or times out.
  EXPECT_TRUE(result->is_response());
}

// The view tree topology changes in the following manner in this test:-
//  root view     root_view       root view
//             ->     |       ->
//               child_view       child view
// |Watch()| on the child viewRef will always return once the view gets connected to the root view
// provided that the view is not destroyed.
TEST_F(FlatlandViewRefInstalledIntegrationTest,
       InstalledAndDisconnectedViewRef_ShouldReturnResponse) {
  // Create the child view and connect it to the root view.
  fuc::FlatlandPtr child_session;
  child_session = realm_->Connect<fuc::Flatland>();

  auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
  fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
  fuc::ViewBoundProtocols protocols;
  auto identity = scenic::NewViewIdentityOnCreation();
  fuv::ViewRef view_ref_clone;
  fidl::Clone(identity.view_ref, &view_ref_clone);
  ConnectChildView(root_session_, std::move(parent_token));

  child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                             parent_viewport_watcher.NewRequest());

  BlockingPresent(child_session);

  // Disconnect the child view.
  child_session->SetRootTransform({0});
  BlockingPresent(child_session);

  // Watch should still return true, since the view has been previously installed.
  std::optional<WatchResult> result;
  view_ref_installed_ptr_->Watch(std::move(view_ref_clone), [&result](auto watch_result) {
    result.emplace(std::move(watch_result));
  });
  RunLoopUntil([&result] { return result.has_value(); });  // Succeeds or times out.
  EXPECT_TRUE(result->is_response());
}

// The view tree topology changes in the following manner in this test:-
//  root view     root_view       root view
//             ->     |       ->
//               child_view
// |Watch()| on the child viewRef will return an error since the child view is released.
TEST_F(FlatlandViewRefInstalledIntegrationTest, InstalledAndDestroyedViewRef_ShouldReturnError) {
  // Create the child view and connect it to the root view.
  fuc::FlatlandPtr child_session;
  child_session = realm_->Connect<fuc::Flatland>();

  auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
  fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
  fuc::ViewBoundProtocols protocols;
  auto identity = scenic::NewViewIdentityOnCreation();
  fuv::ViewRef view_ref_clone;
  fidl::Clone(identity.view_ref, &view_ref_clone);
  ConnectChildView(root_session_, std::move(parent_token));

  child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                             parent_viewport_watcher.NewRequest());

  BlockingPresent(child_session);

  // Release the child view.
  child_session->ReleaseView();
  BlockingPresent(child_session);

  std::optional<WatchResult> result;
  view_ref_installed_ptr_->Watch(std::move(view_ref_clone), [&result](auto watch_result) {
    result.emplace(std::move(watch_result));
  });
  RunLoopUntil([&result] { return result.has_value(); });  // Succeeds or times out.
  EXPECT_TRUE(result->is_err());
}

// The test exercises a three node topology:-
//  root_view
//      |
//  parent_view
//      |
//   child view
// |Watch()| on the child viewRef returns only when the child view gets connected to the root of the
// graph transitively.
TEST_F(FlatlandViewRefInstalledIntegrationTest, TransitiveConnection_ShouldReturnResponse) {
  // Create the parent view and connect it to the root view.
  fuc::FlatlandPtr parent_session;
  parent_session = realm_->Connect<fuc::Flatland>();
  auto [parent_view_token, parent_viewport_token] = scenic::ViewCreationTokenPair::New();

  {
    fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
    fuc::ViewBoundProtocols protocols;
    auto identity = scenic::NewViewIdentityOnCreation();

    parent_session->CreateView2(std::move(parent_view_token), std::move(identity),
                                std::move(protocols), parent_viewport_watcher.NewRequest());

    BlockingPresent(parent_session);
  }

  // Create the child view and connect it to the parent view.
  fuc::FlatlandPtr child_session;
  child_session = realm_->Connect<fuc::Flatland>();
  fuv::ViewRef child_view_ref;
  {
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
    fuc::ViewBoundProtocols protocols;
    auto identity = scenic::NewViewIdentityOnCreation();
    fidl::Clone(identity.view_ref, &child_view_ref);
    ConnectChildView(parent_session, std::move(parent_token));

    child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                               parent_viewport_watcher.NewRequest());

    BlockingPresent(child_session);
  }

  std::optional<WatchResult> result;
  view_ref_installed_ptr_->Watch(std::move(child_view_ref), [&result](auto watch_result) {
    result.emplace(std::move(watch_result));
  });
  // child view ref not installed; should not return yet.
  RunLoopUntilIdle();
  EXPECT_FALSE(result.has_value());

  // Now attach the whole thing to the root and observe that the child view ref is installed.
  ConnectChildView(root_session_, std::move(parent_viewport_token));
  RunLoopUntil([&result] { return result.has_value(); });  // Succeeds or times out.
  EXPECT_TRUE(result->is_response());
}

}  // namespace integration_tests
