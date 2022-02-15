// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/observation/test/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-loop/testing/cpp/real_loop.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_identity.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include <map>
#include <string>
#include <vector>

#include <sdk/lib/ui/scenic/cpp/view_creation_tokens.h>
#include <zxtest/zxtest.h>

#include "src/ui/scenic/integration_tests/scenic_realm_builder.h"
#include "src/ui/scenic/integration_tests/utils.h"

// This test exercises the fuchsia.ui.observation.test.Registry protocol implemented by Scenic.

namespace {
using ExpectedLayout = std::pair<float, float>;

// Stores information about a view node present in a fuog_ViewDescriptor. Used for assertions.
struct SnapshotViewNode {
  std::optional<zx_koid_t> view_ref_koid;
  std::vector<uint32_t> children;
  std::optional<ExpectedLayout> layout;
};

// A helper class for creating a SnapshotViewNode vector.
class ViewBuilder {
 public:
  static ViewBuilder New() { return ViewBuilder(); }

  ViewBuilder& AddView(std::optional<zx_koid_t> view_ref_koid, std::vector<uint32_t> children,
                       std::optional<ExpectedLayout> layout = std::nullopt) {
    SnapshotViewNode view_node = {
        .view_ref_koid = view_ref_koid, .children = std::move(children), .layout = layout};
    snapshot_view_nodes_.push_back(std::move(view_node));
    return *this;
  }

  std::vector<SnapshotViewNode> Build() { return snapshot_view_nodes_; }

 private:
  std::vector<SnapshotViewNode> snapshot_view_nodes_;
};

}  // namespace

namespace integration_tests {
using fuc_ChildViewWatcher = fuchsia::ui::composition::ChildViewWatcher;
using fuc_ContentId = fuchsia::ui::composition::ContentId;
using fuc_Flatland = fuchsia::ui::composition::Flatland;
using fuc_FlatlandDisplay = fuchsia::ui::composition::FlatlandDisplay;
using fuc_FlatlandDisplayPtr = fuchsia::ui::composition::FlatlandDisplayPtr;
using fuc_FlatlandPtr = fuchsia::ui::composition::FlatlandPtr;
using fuc_ParentViewportWatcher = fuchsia::ui::composition::ParentViewportWatcher;
using fuc_TransformId = fuchsia::ui::composition::TransformId;
using fuc_ViewBoundProtocols = fuchsia::ui::composition::ViewBoundProtocols;
using fuc_ViewportProperties = fuchsia::ui::composition::ViewportProperties;
using fuf_FocusChain = fuchsia::ui::focus::FocusChain;
using fuf_FocusChainListener = fuchsia::ui::focus::FocusChainListener;
using fuf_FocusChainListenerRegistry = fuchsia::ui::focus::FocusChainListenerRegistry;
using fuog_ProviderPtr = fuchsia::ui::observation::geometry::ProviderPtr;
using fuog_ProviderWatchResponse = fuchsia::ui::observation::geometry::ProviderWatchResponse;
using fuog_ViewDescriptor = fuchsia::ui::observation::geometry::ViewDescriptor;
using fuog_ViewTreeSnapshot = fuchsia::ui::observation::geometry::ViewTreeSnapshot;
using fuot_Registry = fuchsia::ui::observation::test::Registry;
using fuot_RegistryPtr = fuchsia::ui::observation::test::RegistryPtr;
using fus_Scenic = fuchsia::ui::scenic::Scenic;
using fus_ScenicPtr = fuchsia::ui::scenic::ScenicPtr;
using fus_SessionEndpoints = fuchsia::ui::scenic::SessionEndpoints;
using fus_SessionListenerHandle = fuchsia::ui::scenic::SessionListenerHandle;
using fus_SessionPtr = fuchsia::ui::scenic::SessionPtr;
using fuv_FocuserPtr = fuchsia::ui::views::FocuserPtr;
using fuv_ViewRef = fuchsia::ui::views::ViewRef;
using fuv_ViewRefFocusedPtr = fuchsia::ui::views::ViewRefFocusedPtr;
using fuv_ViewportCreationToken = fuchsia::ui::views::ViewportCreationToken;
using RealmRoot = sys::testing::experimental::RealmRoot;

scenic::Session CreateSession(fus_Scenic* scenic, fus_SessionEndpoints endpoints) {
  FX_DCHECK(scenic);
  FX_DCHECK(!endpoints.has_session());
  FX_DCHECK(!endpoints.has_session_listener());

  fus_SessionPtr session_ptr;
  fus_SessionListenerHandle listener_handle;
  auto listener_request = listener_handle.NewRequest();

  endpoints.set_session(session_ptr.NewRequest());
  endpoints.set_session_listener(std::move(listener_handle));
  scenic->CreateSessionT(std::move(endpoints), [] {});

  return scenic::Session(std::move(session_ptr), std::move(listener_request));
}

// Sets up the root of a scene.
// Present() must be called separately by the creator, since this does not have access to the
// looper.
struct GfxRootSession {
  GfxRootSession(fus_Scenic* scenic)
      : session(CreateSession(scenic, {})),
        compositor(&session),
        layer_stack(&session),
        layer(&session),
        renderer(&session),
        scene(&session),
        camera(scene) {
    compositor.SetLayerStack(layer_stack);
    layer_stack.AddLayer(layer);
    layer.SetRenderer(renderer);
    renderer.SetCamera(camera);
  }

  scenic::Session session;
  scenic::DisplayCompositor compositor;
  scenic::LayerStack layer_stack;
  scenic::Layer layer;
  scenic::Renderer renderer;
  scenic::Scene scene;
  scenic::Camera camera;

  std::unique_ptr<scenic::ViewHolder> view_holder;
};

void AssertViewDescriptor(const fuog_ViewDescriptor& view_descriptor,
                          const SnapshotViewNode& expected_view_descriptor) {
  if (expected_view_descriptor.view_ref_koid.has_value()) {
    ASSERT_TRUE(view_descriptor.has_view_ref_koid());
    EXPECT_EQ(view_descriptor.view_ref_koid(), expected_view_descriptor.view_ref_koid.value());
  }

  ASSERT_TRUE(view_descriptor.has_children());
  ASSERT_EQ(view_descriptor.children().size(), expected_view_descriptor.children.size());
  for (uint32_t i = 0; i < view_descriptor.children().size(); i++) {
    EXPECT_EQ(view_descriptor.children()[i], expected_view_descriptor.children[i]);
  }

  if (expected_view_descriptor.layout.has_value()) {
    ASSERT_TRUE(view_descriptor.has_layout());
    auto& layout = view_descriptor.layout();

    EXPECT_TRUE(CmpFloatingValues(layout.extent.min.x, 0.));
    EXPECT_TRUE(CmpFloatingValues(layout.extent.min.y, 0.));
    EXPECT_TRUE(CmpFloatingValues(layout.extent.max.x, expected_view_descriptor.layout->first));
    EXPECT_TRUE(CmpFloatingValues(layout.extent.max.y, expected_view_descriptor.layout->second));
    EXPECT_TRUE(CmpFloatingValues(layout.pixel_scale[0], 1.f));
    EXPECT_TRUE(CmpFloatingValues(layout.pixel_scale[1], 1.f));
  }
}

void AssertViewTreeSnapshot(const fuog_ViewTreeSnapshot& snapshot,
                            std::vector<SnapshotViewNode> expected_snapshot_nodes) {
  ASSERT_TRUE(snapshot.has_views());
  ASSERT_EQ(snapshot.views().size(), expected_snapshot_nodes.size());

  for (uint32_t i = 0; i < snapshot.views().size(); i++) {
    AssertViewDescriptor(snapshot.views()[i], expected_snapshot_nodes[i]);
  }
}

// Test fixture that sets up an environment with Registry protocol we can connect to. This test
// fixture is used for tests where the view nodes are created by Flatland instances.
class FlatlandObserverRegistryIntegrationTest : public zxtest::Test,
                                                public loop_fixture::RealLoop,
                                                public fuf_FocusChainListener {
 protected:
  FlatlandObserverRegistryIntegrationTest() : focus_chain_listener_(this) {}

  void SetUp() override {
    // Build the realm topology and route the protocols required by this test fixture from the
    // scenic subrealm.
    realm_ = ScenicRealmBuilder(
                 "fuchsia-pkg://fuchsia.com/observer_integration_tests#meta/scenic_subrealm.cm")
                 .AddScenicSubRealmProtocol(fuchsia::ui::observation::test::Registry::Name_)
                 .AddScenicSubRealmProtocol(fuchsia::ui::composition::Flatland::Name_)
                 .AddScenicSubRealmProtocol(fuchsia::ui::composition::FlatlandDisplay::Name_)
                 .AddScenicSubRealmProtocol(fuchsia::ui::composition::Allocator::Name_)
                 .AddScenicSubRealmProtocol(fuchsia::ui::focus::FocusChainListenerRegistry::Name_)
                 .Build();

    // Set up focus chain listener and wait for the initial null focus chain.
    fidl::InterfaceHandle<fuf_FocusChainListener> listener_handle;
    focus_chain_listener_.Bind(listener_handle.NewRequest());
    auto focus_chain_listener_registry = realm_->Connect<fuf_FocusChainListenerRegistry>();
    focus_chain_listener_registry->Register(std::move(listener_handle));
    EXPECT_EQ(CountReceivedFocusChains(), 0u);
    RunLoopUntil([this] { return CountReceivedFocusChains() == 1u; });

    observer_registry_ptr_ = realm_->Connect<fuot_Registry>();

    observer_registry_ptr_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to Observer Registry Protocol: %s", zx_status_get_string(status));
    });

    flatland_display_ = realm_->Connect<fuc_FlatlandDisplay>();
    flatland_display_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
    });

    // Set up root view.
    root_session_ = realm_->Connect<fuc_Flatland>();
    root_session_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
    });

    fidl::InterfacePtr<fuc_ChildViewWatcher> child_view_watcher;
    fuc_ViewBoundProtocols protocols;
    protocols.set_view_focuser(root_focuser_.NewRequest());
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    flatland_display_->SetContent(std::move(parent_token), child_view_watcher.NewRequest());
    fidl::InterfacePtr<fuc_ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    root_view_ref_ = fidl::Clone(identity.view_ref);
    root_session_->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                               parent_viewport_watcher.NewRequest());
    parent_viewport_watcher->GetLayout([this](auto layout_info) {
      ASSERT_TRUE(layout_info.has_logical_size());
      const auto [width, height] = layout_info.logical_size();
      display_width_ = static_cast<float>(width);
      display_height_ = static_cast<float>(height);
    });
    BlockingPresent(root_session_);

    // Now that the scene exists, wait for a valid focus chain and for the display size.
    RunLoopUntil([this] {
      return CountReceivedFocusChains() == 2u && display_width_ != 0 && display_height_ != 0;
    });
  }

  // Invokes Flatland.Present() and waits for a response from Scenic that the frame has been
  // presented.
  void BlockingPresent(fuc_FlatlandPtr& flatland) {
    bool presented = false;
    flatland.events().OnFramePresented = [&presented](auto) { presented = true; };
    flatland->Present({});
    RunLoopUntil([&presented] { return presented; });
    flatland.events().OnFramePresented = nullptr;
  }

  // Create a new transform and viewport, then call |BlockingPresent| to wait for it to take
  // effect. This can be called only once per Flatland instance, because it uses hard-coded IDs for
  // the transform and viewport.
  void ConnectChildView(fuc_FlatlandPtr& flatland, fuv_ViewportCreationToken&& token) {
    // Let the client_end die.
    fidl::InterfacePtr<fuc_ChildViewWatcher> child_view_watcher;
    fuc_ViewportProperties properties;
    properties.set_logical_size({kDefaultSize, kDefaultSize});

    fuc_TransformId kTransform{.value = 1};
    flatland->CreateTransform(kTransform);
    flatland->SetRootTransform(kTransform);

    const fuc_ContentId kContent{.value = 1};
    flatland->CreateViewport(kContent, std::move(token), std::move(properties),
                             child_view_watcher.NewRequest());
    flatland->SetContent(kTransform, kContent);

    BlockingPresent(flatland);
  }

  // |fuchsia::ui::focus::FocusChainListener|
  void OnFocusChange(fuf_FocusChain focus_chain, OnFocusChangeCallback callback) override {
    observed_focus_chains_.push_back(std::move(focus_chain));
    callback();  // Receipt.
  }

  size_t CountReceivedFocusChains() const { return observed_focus_chains_.size(); }

  const uint32_t kDefaultSize = 1;
  float display_width_ = 0;
  float display_height_ = 0;
  fuot_RegistryPtr observer_registry_ptr_;
  fuc_FlatlandPtr root_session_;
  fuv_ViewRef root_view_ref_;
  fuv_FocuserPtr root_focuser_;
  std::unique_ptr<RealmRoot> realm_;

 private:
  fuc_FlatlandDisplayPtr flatland_display_;
  fidl::Binding<fuf_FocusChainListener> focus_chain_listener_;
  std::vector<fuf_FocusChain> observed_focus_chains_;
};

// Test fixture that sets up an environment with Registry protocol we can connect to. This test
// fixture is used for tests where the view nodes are created by GFX instances.
class GfxObserverRegistryIntegrationTest : public zxtest::Test, public loop_fixture::RealLoop {
 protected:
  fus_Scenic* scenic() { return scenic_.get(); }

  void SetUp() override {
    // Build the realm topology and route the protocols required by this test fixture from the
    // scenic subrealm.
    realm_ = ScenicRealmBuilder(
                 "fuchsia-pkg://fuchsia.com/observer_integration_tests#meta/scenic_subrealm.cm")
                 .AddScenicSubRealmProtocol(fuchsia::ui::observation::test::Registry::Name_)
                 .AddScenicSubRealmProtocol(fuchsia::ui::composition::Flatland::Name_)
                 .AddScenicSubRealmProtocol(fuchsia::ui::composition::FlatlandDisplay::Name_)
                 .AddScenicSubRealmProtocol(fuchsia::ui::composition::Allocator::Name_)
                 .AddScenicSubRealmProtocol(fuchsia::ui::scenic::Scenic::Name_)
                 .Build();

    scenic_ = realm_->Connect<fus_Scenic>();
    scenic_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
    });

    observer_registry_ptr_ = realm_->Connect<fuot_Registry>();
    observer_registry_ptr_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to Observer Registry Protocol: %s", zx_status_get_string(status));
    });

    // Set up root session.
    root_session_ = std::make_unique<GfxRootSession>(scenic());
    root_session_->session.set_error_handler([](zx_status_t status) {
      FAIL("Root session terminated: %s", zx_status_get_string(status));
    });
    BlockingPresent(root_session_->session);
  }

  // Invokes GFX.Present2() and waits for a response from Scenic that the frame has been
  // presented.
  void BlockingPresent(scenic::Session& session) {
    bool presented = false;
    session.set_on_frame_presented_handler([&presented](auto) { presented = true; });
    session.Present2(0, 0, [](auto) {});
    RunLoopUntil([&presented] { return presented; });
    session.set_on_frame_presented_handler([](auto) {});
  }

  fuot_RegistryPtr observer_registry_ptr_;
  std::unique_ptr<GfxRootSession> root_session_;
  std::unique_ptr<RealmRoot> realm_;

 private:
  fus_ScenicPtr scenic_;
};

TEST_F(FlatlandObserverRegistryIntegrationTest, RegistryProtocolConnectedSuccess) {
  fuog_ProviderPtr geometry_provider;
  std::optional<bool> result;
  observer_registry_ptr_->RegisterGlobalGeometryProvider(geometry_provider.NewRequest(),
                                                         [&result] { result = true; });
  RunLoopUntil([&result] { return result.has_value(); });
  EXPECT_TRUE(result.value());
}

// The client should receive updates whenever there is a change in the topology of the view tree.
// The view tree topology changes in the following manner in this test:
// root_view -> root_view    ->   root_view   ->  root_view
//                  |                 |               |
//            parent_view       parent_view     parent_view
//                                    |
//                               child_view
TEST_F(FlatlandObserverRegistryIntegrationTest, ClientReceivesTopologyUpdatesForFlatland) {
  fuog_ProviderPtr geometry_provider;
  std::optional<bool> result;
  observer_registry_ptr_->RegisterGlobalGeometryProvider(geometry_provider.NewRequest(),
                                                         [&result] { result = true; });

  RunLoopUntil([&result] { return result.has_value(); });
  EXPECT_TRUE(result.value());

  // Set up the parent_view and connect it to the root_view.
  fuc_FlatlandPtr parent_session;
  fuv_ViewRef parent_view_ref;
  {
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    parent_session = realm_->Connect<fuc_Flatland>();
    fidl::InterfacePtr<fuc_ParentViewportWatcher> parent_viewport_watcher;
    fuc_ViewBoundProtocols protocols;
    auto identity = scenic::NewViewIdentityOnCreation();
    parent_view_ref = fidl::Clone(identity.view_ref);

    ConnectChildView(root_session_, std::move(parent_token));

    parent_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                                parent_viewport_watcher.NewRequest());

    BlockingPresent(parent_session);
  }

  // Set up the child_view and connect it to the parent_view.
  fuc_FlatlandPtr child_session;
  fuv_ViewRef child_view_ref;
  {
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    child_session = realm_->Connect<fuc_Flatland>();
    fidl::InterfacePtr<fuc_ParentViewportWatcher> parent_viewport_watcher;
    fuc_ViewBoundProtocols protocols;
    auto identity = scenic::NewViewIdentityOnCreation();
    child_view_ref = fidl::Clone(identity.view_ref);

    ConnectChildView(parent_session, std::move(parent_token));

    child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                               parent_viewport_watcher.NewRequest());

    BlockingPresent(child_session);
  }

  // Detach the child_view from the parent_view.
  child_session->ReleaseView();
  BlockingPresent(child_session);

  std::optional<fuog_ProviderWatchResponse> geometry_result;

  geometry_provider->Watch(
      [&geometry_result](auto response) { geometry_result = std::move(response); });

  RunLoopUntil([&geometry_result] { return geometry_result.has_value(); });

  EXPECT_FALSE(geometry_result->has_error());

  // The total number of updates generated is equal to the number of |Present| calls made.
  ASSERT_TRUE(geometry_result->has_updates());
  ASSERT_EQ(geometry_result->updates().size(), 5UL);

  auto root_view_ref_koid = ExtractKoid(root_view_ref_);
  auto parent_view_ref_koid = ExtractKoid(parent_view_ref);
  auto child_view_ref_koid = ExtractKoid(child_view_ref);

  // This snapshot captures the state of the view tree when the scene only has the root_view.
  AssertViewTreeSnapshot(geometry_result->updates()[0],
                         ViewBuilder().AddView(root_view_ref_koid, {}).Build());

  // This snapshot captures the state of the view tree when parent_view gets connected to the
  // root_view.
  AssertViewTreeSnapshot(
      geometry_result->updates()[1],
      ViewBuilder()
          .AddView(root_view_ref_koid, {static_cast<uint32_t>(parent_view_ref_koid)})
          .AddView(parent_view_ref_koid, {})
          .Build());

  // This snapshot captures the state of the view tree when child_view gets connected to the
  // parent_view.
  AssertViewTreeSnapshot(
      geometry_result->updates()[3],
      ViewBuilder()
          .AddView(root_view_ref_koid, {static_cast<uint32_t>(parent_view_ref_koid)})
          .AddView(parent_view_ref_koid, {static_cast<uint32_t>(child_view_ref_koid)})
          .AddView(child_view_ref_koid, {})
          .Build());

  // This snapshot captures the state of the view tree when child_view detaches from the
  // parent_view.
  AssertViewTreeSnapshot(
      geometry_result->updates()[4],
      ViewBuilder()
          .AddView(root_view_ref_koid, {static_cast<uint32_t>(parent_view_ref_koid)})
          .AddView(parent_view_ref_koid, {})
          .Build());
}

TEST_F(FlatlandObserverRegistryIntegrationTest, ClientReceivesLayoutUpdatesForFlatland) {
  fuog_ProviderPtr geometry_provider;
  std::optional<bool> result;
  observer_registry_ptr_->RegisterGlobalGeometryProvider(geometry_provider.NewRequest(),
                                                         [&result] { result = true; });

  RunLoopUntil([&result] { return result.has_value(); });
  EXPECT_TRUE(result.value());

  // Set up a child view and connect it to the root view.
  fuc_FlatlandPtr session;

  auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
  session = realm_->Connect<fuc_Flatland>();
  fidl::InterfacePtr<fuc_ParentViewportWatcher> parent_viewport_watcher;
  fuc_ViewBoundProtocols protocols;
  auto identity = scenic::NewViewIdentityOnCreation();
  fuv_ViewRef view_ref = fidl::Clone(identity.view_ref);

  ConnectChildView(root_session_, std::move(parent_token));

  session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                       parent_viewport_watcher.NewRequest());

  BlockingPresent(session);

  // Modify the Viewport properties of the root.
  fuc_ViewportProperties properties;
  const int32_t width = 100, height = 100;
  properties.set_logical_size({width, height});
  root_session_->SetViewportProperties({1}, std::move(properties));

  BlockingPresent(root_session_);

  std::optional<fuog_ProviderWatchResponse> geometry_result;

  geometry_provider->Watch(
      [&geometry_result](auto response) { geometry_result = std::move(response); });

  RunLoopUntil([&geometry_result] { return geometry_result.has_value(); });

  EXPECT_FALSE(geometry_result->has_error());

  // The total number of updates generated is equal to the number of |Present| calls made.
  ASSERT_TRUE(geometry_result->has_updates());
  ASSERT_EQ(geometry_result->updates().size(), 3UL);

  auto root_view_ref_koid = ExtractKoid(root_view_ref_);
  auto child_view_ref_koid = ExtractKoid(view_ref);

  // This snapshot captures the state of the view tree when the root view sets the logical size
  // of the viewport as {|kDefaultSize|,|kDefaultSize|}.
  AssertViewTreeSnapshot(
      geometry_result->updates()[1],
      ViewBuilder()
          .AddView(root_view_ref_koid, {static_cast<uint32_t>(child_view_ref_koid)},
                   std::make_pair(display_width_, display_height_))
          .AddView(child_view_ref_koid, {}, std::make_pair(kDefaultSize, kDefaultSize))
          .Build());

  // This snapshot captures the state of the view tree when the root view sets the logical size
  // of the viewport as {|width|,|height|}.
  AssertViewTreeSnapshot(
      geometry_result->updates()[2],
      ViewBuilder()
          .AddView(root_view_ref_koid, {static_cast<uint32_t>(child_view_ref_koid)},
                   std::make_pair(display_width_, display_height_))
          .AddView(child_view_ref_koid, {}, std::make_pair(width, height))
          .Build());
}

// A view present in a fuog_ViewTreeSnapshot must be present in the view tree and should be
// focusable and hittable. In this test, the client (root view) uses |f.u.o.g.Provider| to get
// notified about a child view getting connected and then moves focus to the child view.
TEST_F(FlatlandObserverRegistryIntegrationTest, ChildRequestsFocusAfterConnectingForFlatland) {
  fuog_ProviderPtr geometry_provider;
  std::optional<bool> result;
  observer_registry_ptr_->RegisterGlobalGeometryProvider(geometry_provider.NewRequest(),
                                                         [&result] { result = true; });

  RunLoopUntil([&result] { return result.has_value(); });
  EXPECT_TRUE(result.value());

  // Set up the child view and connect it to the root view.
  fuc_FlatlandPtr child_session;
  fuv_ViewRef child_view_ref;
  fuv_ViewRefFocusedPtr child_focused_ptr;
  {
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    child_session = realm_->Connect<fuc_Flatland>();
    fidl::InterfacePtr<fuc_ParentViewportWatcher> parent_viewport_watcher;
    fuc_ViewBoundProtocols protocols;
    protocols.set_view_ref_focused(child_focused_ptr.NewRequest());
    auto identity = scenic::NewViewIdentityOnCreation();
    child_view_ref = fidl::Clone(identity.view_ref);

    ConnectChildView(root_session_, std::move(parent_token));

    child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                               parent_viewport_watcher.NewRequest());

    BlockingPresent(child_session);
  }

  // Watch for child focused event.
  std::optional<bool> child_focused;
  child_focused_ptr->Watch([&child_focused](auto update) {
    ASSERT_TRUE(update.has_focused());
    child_focused = update.focused();
  });

  std::optional<fuog_ProviderWatchResponse> geometry_result;

  geometry_provider->Watch(
      [&geometry_result](auto response) { geometry_result = std::move(response); });

  RunLoopUntil([&geometry_result] { return geometry_result.has_value(); });

  // The total number of updates generated is equal to the number of |Present| calls made.
  ASSERT_TRUE(geometry_result->has_updates());
  ASSERT_FALSE(geometry_result->has_error());
  ASSERT_EQ(geometry_result->updates().size(), 2UL);

  // This snapshot captures the state of the view tree when the child view gets connected to the
  // root view.
  auto& snapshot = geometry_result->updates()[1];
  auto& root_view_descriptor = snapshot.views()[0];
  auto& children = root_view_descriptor.children();

  auto child_view_ref_koid = ExtractKoid(child_view_ref);

  // Root view moves focus to the child view after it shows up in the fuog_ViewTreeSnapshot.
  std::optional<bool> request_processed = false;
  root_focuser_->RequestFocus(fidl::Clone(child_view_ref), [&request_processed](auto result) {
    request_processed = true;
    FX_DCHECK(!result.is_err());
  });

  RunLoopUntil([&children, &request_processed, &child_focused, &child_view_ref_koid] {
    return std::find(children.begin(), children.end(), child_view_ref_koid) != children.end() &&
           request_processed.has_value() && child_focused.has_value();
  });

  // Child view should receive focus when it gets connected to the root view.
  EXPECT_TRUE(request_processed.value());
  EXPECT_TRUE(child_focused.value());
}

// The client should receive updates whenever there is a change in the topology of the view tree.
// The view tree topology changes in the following manner in this test:
// root_view -> root_view    ->   root_view   ->  root_view
//                  |                 |               |
//            parent_view       parent_view     parent_view
//                                    |
//                               child_view
TEST_F(GfxObserverRegistryIntegrationTest, ClientReceivesHierarchyUpdatesForGfx) {
  fuog_ProviderPtr geometry_provider;
  std::optional<bool> result;
  observer_registry_ptr_->RegisterGlobalGeometryProvider(geometry_provider.NewRequest(),
                                                         [&result] { result = true; });

  RunLoopUntil([&result] { return result.has_value(); });
  EXPECT_TRUE(result.value());

  // Set up the parent_view and connect it to the root_view.
  scenic::Session parent_session = CreateSession(scenic(), {});
  fuv_ViewRef parent_view_ref_copy;
  auto [parent_view_token, parent_view_holder_token] = scenic::ViewTokenPair::New();
  auto [parent_control_ref, parent_view_ref] = scenic::ViewRefPair::New();
  fidl::Clone(parent_view_ref, &parent_view_ref_copy);

  scenic::View parent_view(&parent_session, std::move(parent_view_token),
                           std::move(parent_control_ref), std::move(parent_view_ref),
                           "parent_view");

  root_session_->view_holder = std::make_unique<scenic::ViewHolder>(
      &root_session_->session, std::move(parent_view_holder_token), "parent_holder");

  root_session_->scene.AddChild(*root_session_->view_holder);

  BlockingPresent(root_session_->session);
  BlockingPresent(parent_session);

  // Set up the child_view and connect it to the parent_view.
  scenic::Session child_session = CreateSession(scenic(), {});
  fuv_ViewRef child_view_ref_copy;

  auto [child_view_token, child_view_holder_token] = scenic::ViewTokenPair::New();
  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();

  fidl::Clone(child_view_ref, &child_view_ref_copy);
  scenic::View child_view(&child_session, std::move(child_view_token), std::move(child_control_ref),
                          std::move(child_view_ref), "child_view");

  scenic::ViewHolder child_view_holder(&parent_session, std::move(child_view_holder_token),
                                       "child_holder");

  parent_view.AddChild(child_view_holder);

  BlockingPresent(child_session);
  BlockingPresent(parent_session);

  // Detach the child_view from the parent_view.
  parent_view.DetachChild(child_view_holder);
  BlockingPresent(parent_session);

  std::optional<fuog_ProviderWatchResponse> geometry_result;

  geometry_provider->Watch(
      [&geometry_result](auto response) { geometry_result = std::move(response); });

  RunLoopUntil([&geometry_result] { return geometry_result.has_value(); });

  EXPECT_FALSE(geometry_result->has_error());

  // The total number of updates generated is equal to the number of |Present| calls made.
  ASSERT_TRUE(geometry_result->has_updates());
  ASSERT_EQ(geometry_result->updates().size(), 5UL);

  auto parent_view_ref_koid = ExtractKoid(parent_view_ref_copy);
  auto child_view_ref_koid = ExtractKoid(child_view_ref_copy);

  // This snapshot captures the state of the view tree when the scene only has the root_view.
  AssertViewTreeSnapshot(geometry_result->updates()[0],
                         ViewBuilder().AddView(std::nullopt, {}).Build());

  // This snapshot captures the state of the view tree when parent_view gets connected to the
  // root_view.
  AssertViewTreeSnapshot(geometry_result->updates()[1],
                         ViewBuilder()
                             .AddView(std::nullopt, {static_cast<uint32_t>(parent_view_ref_koid)})
                             .AddView(parent_view_ref_koid, {})
                             .Build());

  // This snapshot captures the state of the view tree when child_view gets connected to the
  // parent_view.
  AssertViewTreeSnapshot(
      geometry_result->updates()[3],
      ViewBuilder()
          .AddView(std::nullopt, {static_cast<uint32_t>(parent_view_ref_koid)})
          .AddView(parent_view_ref_koid, {static_cast<uint32_t>(child_view_ref_koid)})
          .AddView(child_view_ref_koid, {})
          .Build());

  // This snapshot captures the state of the view tree when child_view detaches from the
  // parent_view.
  AssertViewTreeSnapshot(geometry_result->updates()[4],
                         ViewBuilder()
                             .AddView(std::nullopt, {static_cast<uint32_t>(parent_view_ref_koid)})
                             .AddView(parent_view_ref_koid, {})
                             .Build());
}

}  // namespace integration_tests
