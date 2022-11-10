// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/display/singleton/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async-loop/testing/cpp/real_loop.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/view_creation_tokens.h>
#include <lib/ui/scenic/cpp/view_identity.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <optional>

#include <zxtest/zxtest.h>

#include "src/ui/scenic/tests/utils/scenic_realm_builder.h"
#include "src/ui/scenic/tests/utils/utils.h"

// This test exercises a two node topology and tests the signals propagated between the
// parent instance and the child instance.
namespace integration_tests {

namespace fuc = fuchsia::ui::composition;
namespace fuv = fuchsia::ui::views;

using RealmRoot = component_testing::RealmRoot;

constexpr fuc::TransformId kTransformId = {1};
constexpr fuc::ContentId kContentId = {1};

// Test fixture that sets up an environment with a Scenic we can connect to.
class FlatlandViewIntegrationTest : public zxtest::Test, public loop_fixture::RealLoop {
 protected:
  void SetUp() override {
    // Build the realm topology and route the protocols required by this test fixture from the
    // scenic subrealm.
    realm_ = std::make_unique<RealmRoot>(
        ScenicRealmBuilder()
            .AddRealmProtocol(fuc::Flatland::Name_)
            .AddRealmProtocol(fuc::FlatlandDisplay::Name_)
            .AddRealmProtocol(fuc::Allocator::Name_)
            .AddRealmProtocol(fuchsia::ui::display::singleton::Info::Name_)
            .Build());

    // Create the flatland display.
    flatland_display_ = realm_->Connect<fuc::FlatlandDisplay>();
    flatland_display_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
    });

    // Get the display's width and height.
    auto singleton_display = realm_->Connect<fuchsia::ui::display::singleton::Info>();
    std::optional<fuchsia::ui::display::singleton::Metrics> info;
    singleton_display->GetMetrics([&info](auto result) { info = std::move(result); });
    RunLoopUntil([&info] { return info.has_value(); });

    display_width_ = info->extent_in_px().width;
    display_height_ = info->extent_in_px().height;
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
  void CreateAndSetViewport(fuc::FlatlandPtr& flatland,
                            fuv::ViewportCreationToken&& viewport_creation_token,
                            fidl::InterfacePtr<fuc::ChildViewWatcher>& child_view_watcher) {
    fuc::ViewportProperties properties;
    properties.set_logical_size({display_width_, display_height_});

    flatland->CreateTransform(kTransformId);
    flatland->SetRootTransform(kTransformId);

    flatland->CreateViewport(kContentId, std::move(viewport_creation_token), std::move(properties),
                             child_view_watcher.NewRequest());
    flatland->SetContent(kTransformId, kContentId);

    BlockingPresent(flatland);
  }

  std::unique_ptr<RealmRoot> realm_;
  fuc::FlatlandDisplayPtr flatland_display_;
  uint32_t display_width_ = 0;
  uint32_t display_height_ = 0;
};

TEST_F(FlatlandViewIntegrationTest, ParentViewportWatcherUnbindsOnParentDeath) {
  fuc::FlatlandPtr child;
  auto [child_view_token, parent_viewport_token] = scenic::ViewCreationTokenPair::New();
  fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
  // Create the child view.
  {
    child = realm_->Connect<fuc::Flatland>();

    auto identity = scenic::NewViewIdentityOnCreation();
    child->CreateView2(std::move(child_view_token), std::move(identity), {},
                       parent_viewport_watcher.NewRequest());
    BlockingPresent(child);
  }

  // Create the parent view and connect the child view to it.
  {
    fuc::FlatlandPtr parent;
    parent = realm_->Connect<fuc::Flatland>();
    fidl::InterfacePtr<fuc::ChildViewWatcher> parent_view_watcher;
    auto [parent_view_token, display_viewport_token] = scenic::ViewCreationTokenPair::New();

    // Connect the parent view to the display.
    flatland_display_->SetContent(std::move(display_viewport_token),
                                  parent_view_watcher.NewRequest());

    fidl::InterfacePtr<fuc::ParentViewportWatcher> display_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    parent->CreateView2(std::move(parent_view_token), std::move(identity), {},
                        display_viewport_watcher.NewRequest());
    BlockingPresent(parent);

    // Connect the child view to the parent view.
    fidl::InterfacePtr<fuc::ChildViewWatcher> child_view_watcher;
    CreateAndSetViewport(parent, std::move(parent_viewport_token), child_view_watcher);

    EXPECT_TRUE(parent_viewport_watcher.is_bound());
  }

  // The parent instance goes out of scope and dies. Wait for a frame to guarantee parent's death.
  BlockingPresent(child);
  EXPECT_TRUE(child.is_bound());

  // The ParentViewportWatcher unbinds as the parent died.
  EXPECT_FALSE(parent_viewport_watcher.is_bound());
}

TEST_F(FlatlandViewIntegrationTest, ParentViewportWatcherUnbindsOnInvalidTokenTest) {
  // Create the flatland view.
  fuc::FlatlandPtr flatland;
  flatland = realm_->Connect<fuc::Flatland>();
  fuv::ViewCreationToken invalid_token;

  fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
  auto identity = scenic::NewViewIdentityOnCreation();

  // Use an invalid ViewCreationToken in |CreateView2|.
  flatland->CreateView2(std::move(invalid_token), std::move(identity), {},
                        parent_viewport_watcher.NewRequest());
  RunLoopUntilIdle();

  // The ParentViewportWatcher unbinds as we supply an invalid ViewCreationToken.
  EXPECT_FALSE(parent_viewport_watcher.is_bound());
}

TEST_F(FlatlandViewIntegrationTest, ParentViewportWatcherUnbindsOnReleaseView) {
  // Create the parent view.
  fuc::FlatlandPtr parent;
  parent = realm_->Connect<fuc::Flatland>();
  fidl::InterfacePtr<fuc::ChildViewWatcher> parent_view_watcher;
  auto [parent_view_creation_token, display_viewport_token] = scenic::ViewCreationTokenPair::New();

  // Connect the parent view to the display.
  flatland_display_->SetContent(std::move(display_viewport_token),
                                parent_view_watcher.NewRequest());

  fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
  auto identity = scenic::NewViewIdentityOnCreation();
  parent->CreateView2(std::move(parent_view_creation_token), std::move(identity), {},
                      parent_viewport_watcher.NewRequest());
  BlockingPresent(parent);

  EXPECT_TRUE(parent_viewport_watcher.is_bound());

  // Disconnect the parent view from the root.
  parent->ReleaseView();
  BlockingPresent(parent);

  // The ParentViewportWatcher unbinds as the parent view is now disconnected.
  EXPECT_FALSE(parent_viewport_watcher.is_bound());
}

TEST_F(FlatlandViewIntegrationTest, ChildViewWatcherUnbindsOnChildDeath) {
  fuc::FlatlandPtr parent;

  // Create the parent view and connect it to the display.
  {
    parent = realm_->Connect<fuc::Flatland>();
    fidl::InterfacePtr<fuc::ChildViewWatcher> child_view_watcher;
    auto [child_view_token, parent_viewport_token] = scenic::ViewCreationTokenPair::New();
    flatland_display_->SetContent(std::move(parent_viewport_token),
                                  child_view_watcher.NewRequest());

    fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    parent->CreateView2(std::move(child_view_token), std::move(identity), {},
                        parent_viewport_watcher.NewRequest());
    BlockingPresent(parent);
  }

  fidl::InterfacePtr<fuc::ChildViewWatcher> child_view_watcher;

  // Create the child view and connect it to the parent view.
  {
    fuc::FlatlandPtr child;
    child = realm_->Connect<fuc::Flatland>();
    auto [child_view_token, parent_viewport_token] = scenic::ViewCreationTokenPair::New();
    fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    child->CreateView2(std::move(child_view_token), std::move(identity), {},
                       parent_viewport_watcher.NewRequest());
    BlockingPresent(child);

    CreateAndSetViewport(parent, std::move(parent_viewport_token), child_view_watcher);

    EXPECT_TRUE(child_view_watcher.is_bound());
  }

  // The child instance dies as it goes out of scope. Wait for a frame to guarantee child's death.
  BlockingPresent(parent);

  // The ChildViewWatcher unbinds as the child instance died.
  EXPECT_FALSE(child_view_watcher.is_bound());
}

TEST_F(FlatlandViewIntegrationTest, ChildViewWatcherUnbindsOnInvalidToken) {
  // Create the parent view.
  fuc::FlatlandPtr parent;
  parent = realm_->Connect<fuc::Flatland>();

  fidl::InterfacePtr<fuc::ChildViewWatcher> parent_view_watcher;
  auto [child_view_token, parent_viewport_token] = scenic::ViewCreationTokenPair::New();

  // Connect the parent view to the display.
  flatland_display_->SetContent(std::move(parent_viewport_token), parent_view_watcher.NewRequest());

  fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
  auto identity = scenic::NewViewIdentityOnCreation();
  parent->CreateView2(std::move(child_view_token), std::move(identity), {},
                      parent_viewport_watcher.NewRequest());
  BlockingPresent(parent);

  fuv::ViewportCreationToken invalid_token;
  fidl::InterfacePtr<fuc::ChildViewWatcher> child_view_watcher;

  // Create a viewport using an invalid token.
  fuc::ViewportProperties properties;
  properties.set_logical_size({display_width_, display_height_});

  parent->CreateTransform(kTransformId);
  parent->SetRootTransform(kTransformId);

  parent->CreateViewport(kContentId, std::move(invalid_token), std::move(properties),
                         child_view_watcher.NewRequest());
  parent->SetContent(kTransformId, kContentId);

  RunLoopUntilIdle();

  // ChildViewWatcher unbinds as an invalid token was supplied to |CreateViewport|.
  EXPECT_FALSE(child_view_watcher.is_bound());
}

// This test checks whether the |CONNECTED_TO_DISPLAY| and |DISCONNECTED_FROM_DISPLAY| signals are
// propagated correctly.
TEST_F(FlatlandViewIntegrationTest, ParentViewportStatusTest) {
  fuc::FlatlandPtr parent;
  // Create the parent view and connect it to the display.
  {
    parent = realm_->Connect<fuc::Flatland>();
    fidl::InterfacePtr<fuc::ChildViewWatcher> child_view_watcher;

    auto [child_view_token, parent_viewport_token] = scenic::ViewCreationTokenPair::New();
    flatland_display_->SetContent(std::move(parent_viewport_token),
                                  child_view_watcher.NewRequest());

    fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    parent->CreateView2(std::move(child_view_token), std::move(identity), {},
                        parent_viewport_watcher.NewRequest());
    BlockingPresent(parent);
  }

  fuc::FlatlandPtr child;
  std::optional<fuc::ParentViewportStatus> parent_status;
  fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
  // Create the child view and connect it to the parent.
  {
    child = realm_->Connect<fuc::Flatland>();
    auto [child_view_token, parent_viewport_token] = scenic::ViewCreationTokenPair::New();

    auto identity = scenic::NewViewIdentityOnCreation();
    child->CreateView2(std::move(child_view_token), std::move(identity), {},
                       parent_viewport_watcher.NewRequest());

    parent_viewport_watcher->GetStatus(
        [&parent_status](auto status) { parent_status = std::move(status); });

    BlockingPresent(child);

    fidl::InterfacePtr<fuc::ChildViewWatcher> child_view_watcher;
    CreateAndSetViewport(parent, std::move(parent_viewport_token), child_view_watcher);
  }

  // The child instance gets a |CONNECTED_TO_DISPLAY| signal when the child view is connected to the
  // root and when both the parent and the child call |Present|.
  ASSERT_TRUE(parent_status.has_value());
  EXPECT_EQ(parent_status.value(), fuc::ParentViewportStatus::CONNECTED_TO_DISPLAY);
  parent_status.reset();

  // Disconnect the child view.
  parent->SetContent(kTransformId, {0});
  parent_viewport_watcher->GetStatus(
      [&parent_status](auto status) { parent_status = std::move(status); });

  BlockingPresent(parent);

  // The child view gets the |DISCONNECTED_FROM_DISPLAY| signal as it was disconnected from its
  // parent.
  ASSERT_TRUE(parent_status.has_value());
  EXPECT_EQ(parent_status.value(), fuc::ParentViewportStatus::DISCONNECTED_FROM_DISPLAY);
}

// This test checks whether the |CONTENT_HAS_PRESENTED| signal propagates correctly.
TEST_F(FlatlandViewIntegrationTest, ChildViewStatusTest) {
  fuc::FlatlandPtr parent;
  // Create the parent view and connect it to the display.
  {
    parent = realm_->Connect<fuc::Flatland>();

    auto [child_view_token, parent_viewport_token] = scenic::ViewCreationTokenPair::New();
    fidl::InterfacePtr<fuc::ChildViewWatcher> parent_view_watcher;
    flatland_display_->SetContent(std::move(parent_viewport_token),
                                  parent_view_watcher.NewRequest());

    fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    parent->CreateView2(std::move(child_view_token), std::move(identity), {},
                        parent_viewport_watcher.NewRequest());
    BlockingPresent(parent);
  }

  fuc::FlatlandPtr child;
  fidl::InterfacePtr<fuc::ChildViewWatcher> child_view_watcher;
  std::optional<fuc::ChildViewStatus> child_status;
  // Create the child view and connect it to the parent view.
  {
    child = realm_->Connect<fuc::Flatland>();
    auto [child_view_token, parent_viewport_token] = scenic::ViewCreationTokenPair::New();

    fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    child->CreateView2(std::move(child_view_token), std::move(identity), {},
                       parent_viewport_watcher.NewRequest());

    CreateAndSetViewport(parent, std::move(parent_viewport_token), child_view_watcher);

    child_view_watcher->GetStatus(
        [&child_status](auto status) { child_status = std::move(status); });

    BlockingPresent(child);
  }

  // The parent instance gets the |CONTENT_HAS_PRESENTED| signal when the child view calls
  // |Present|.
  ASSERT_TRUE(child_status.has_value());
  EXPECT_EQ(child_status.value(), fuc::ChildViewStatus::CONTENT_HAS_PRESENTED);
}

TEST_F(FlatlandViewIntegrationTest, GetViewRefTest) {
  fuc::FlatlandPtr parent;
  auto [parent_view_creation_token, display_viewport_token] = scenic::ViewCreationTokenPair::New();
  fidl::InterfacePtr<fuc::ChildViewWatcher> parent_view_watcher;

  // Create the parent view.
  {
    parent = realm_->Connect<fuc::Flatland>();

    fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    parent->CreateView2(std::move(parent_view_creation_token), std::move(identity), {},
                        parent_viewport_watcher.NewRequest());
    BlockingPresent(parent);
  }

  fuc::FlatlandPtr child;
  std::optional<fuc::ChildViewStatus> child_status;
  fidl::InterfacePtr<fuc::ChildViewWatcher> child_view_watcher;
  std::optional<fuv::ViewRef> child_view_ref;
  fuv::ViewRef expected_child_view_ref;

  // Create the child view and connect it to the parent view.
  {
    child = realm_->Connect<fuc::Flatland>();
    auto [child_view_token, parent_viewport_token] = scenic::ViewCreationTokenPair::New();

    fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    fidl::Clone(identity.view_ref, &expected_child_view_ref);
    child->CreateView2(std::move(child_view_token), std::move(identity), {},
                       parent_viewport_watcher.NewRequest());

    CreateAndSetViewport(parent, std::move(parent_viewport_token), child_view_watcher);

    child_view_watcher->GetStatus(
        [&child_status](auto status) { child_status = std::move(status); });

    child_view_watcher->GetViewRef(
        [&child_view_ref](auto view_ref) { child_view_ref = std::move(view_ref); });

    BlockingPresent(child);
  }

  // The parent instance gets the |CONTENT_HAS_PRESENTED| signal when the child view calls
  // |Present|.
  ASSERT_TRUE(child_status.has_value());
  EXPECT_EQ(child_status.value(), fuc::ChildViewStatus::CONTENT_HAS_PRESENTED);

  // Note that although CONTENT_HAS_PRESENTED is signaled, GetViewRef() does not yet return the ref.
  // This is because although the parent and child are connected, neither appears in the global
  // topology, because neither is connected to the root.
  EXPECT_FALSE(child_view_ref.has_value());

  flatland_display_->SetContent(std::move(display_viewport_token),
                                parent_view_watcher.NewRequest());

  // Parent's ChildViewWatcher receives the view ref as it is now connected to the display.
  RunLoopUntil([&child_view_ref] { return child_view_ref.has_value(); });
  EXPECT_EQ(ExtractKoid(*child_view_ref), ExtractKoid(expected_child_view_ref));
}

}  // namespace integration_tests
