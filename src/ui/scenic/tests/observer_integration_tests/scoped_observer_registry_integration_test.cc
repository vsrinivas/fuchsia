// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/observation/scope/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-loop/testing/cpp/real_loop.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_creation_tokens.h>
#include <lib/ui/scenic/cpp/view_identity.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include <map>
#include <string>
#include <vector>

#include <zxtest/zxtest.h>

#include "src/ui/scenic/tests/utils/scenic_realm_builder.h"
#include "src/ui/scenic/tests/utils/utils.h"

// This test exercises the fuchsia.ui.observation.test.Registry protocol implemented by Scenic.

// TODO(fxbug.dev/105706): This test duplicate lots of code in
// observer_registry_integration_test.cc. We should factor the shard pieces into
// a base test class and/or util library.

namespace {

using ExpectedLayout = std::pair<float, float>;

constexpr zx::duration kWatchTimeout = zx::sec(60);

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

  ViewBuilder& AddView(std::optional<zx_koid_t> view_ref_koid, std::vector<zx_koid_t> children,
                       std::optional<ExpectedLayout> layout = std::nullopt) {
    std::vector<uint32_t> view_node_children;
    for (auto child : children) {
      view_node_children.push_back(static_cast<uint32_t>(child));
    }

    SnapshotViewNode view_node = {.view_ref_koid = view_ref_koid,
                                  .children = std::move(view_node_children),
                                  .layout = layout};
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
using fug_DisplayInfo = fuchsia::ui::gfx::DisplayInfo;
using fug_vec2 = fuchsia::ui::gfx::vec2;
using fug_ViewProperties = fuchsia::ui::gfx::ViewProperties;
using fuog_ViewTreeWatcherPtr = fuchsia::ui::observation::geometry::ViewTreeWatcherPtr;
using fuog_WatchResponse = fuchsia::ui::observation::geometry::WatchResponse;
using fuog_ViewDescriptor = fuchsia::ui::observation::geometry::ViewDescriptor;
using fuog_ViewTreeSnapshot = fuchsia::ui::observation::geometry::ViewTreeSnapshot;
using fuos_Registry = fuchsia::ui::observation::scope::Registry;
using fuos_RegistryPtr = fuchsia::ui::observation::scope::RegistryPtr;
using fus_Event = fuchsia::ui::scenic::Event;
using fus_Scenic = fuchsia::ui::scenic::Scenic;
using fus_ScenicPtr = fuchsia::ui::scenic::ScenicPtr;
using fus_SessionEndpoints = fuchsia::ui::scenic::SessionEndpoints;
using fus_SessionListenerHandle = fuchsia::ui::scenic::SessionListenerHandle;
using fus_SessionPtr = fuchsia::ui::scenic::SessionPtr;
using fuv_FocuserPtr = fuchsia::ui::views::FocuserPtr;
using fuv_ViewRef = fuchsia::ui::views::ViewRef;
using fuv_ViewRefFocusedPtr = fuchsia::ui::views::ViewRefFocusedPtr;
using fuv_ViewportCreationToken = fuchsia::ui::views::ViewportCreationToken;
using RealmRoot = component_testing::RealmRoot;

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

struct DisplayDimensions {
  float width = 0.f, height = 0.f;
};

// Sets up the root of a scene.
// Present() must be called separately by the creator, since this does not have access to the
// looper.
struct GfxRootSession {
  GfxRootSession(fus_Scenic* scenic, const DisplayDimensions& display_dimensions,
                 fus_SessionEndpoints endpoints)
      : session(CreateSession(scenic, std::move(endpoints))),
        compositor(&session),
        layer_stack(&session),
        layer(&session),
        renderer(&session),
        scene(&session),
        camera(scene),
        display_dimensions(display_dimensions) {
    compositor.SetLayerStack(layer_stack);
    layer_stack.AddLayer(layer);
    layer.SetRenderer(renderer);
    layer.SetSize(display_dimensions.width, display_dimensions.height);
    renderer.SetCamera(camera);
  }

  scenic::Session session;
  scenic::DisplayCompositor compositor;
  scenic::LayerStack layer_stack;
  scenic::Layer layer;
  scenic::Renderer renderer;
  scenic::Scene scene;
  scenic::Camera camera;
  const DisplayDimensions display_dimensions;
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

bool CheckViewExistsInSnapshot(const fuog_ViewTreeSnapshot& snapshot, zx_koid_t view_ref_koid) {
  auto it = std::find_if(
      snapshot.views().begin(), snapshot.views().end(),
      [view_ref_koid](const auto& view) { return view.view_ref_koid() == view_ref_koid; });
  return it != snapshot.views().end();
}

// Returns the iterator to the first fuog_ViewTreeSnapshot in |updates| having |view_ref_koid|
// present.
std::vector<fuog_ViewTreeSnapshot>::const_iterator GetFirstSnapshotWithView(
    const std::vector<fuog_ViewTreeSnapshot>& updates, zx_koid_t view_ref_koid) {
  return std::find_if(updates.begin(), updates.end(), [view_ref_koid](auto& snapshot) {
    return CheckViewExistsInSnapshot(snapshot, view_ref_koid);
  });
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
    realm_ = std::make_unique<RealmRoot>(
        ScenicRealmBuilder()
            .AddRealmProtocol(fuchsia::ui::observation::scope::Registry::Name_)
            .AddRealmProtocol(fuchsia::ui::composition::Flatland::Name_)
            .AddRealmProtocol(fuchsia::ui::composition::FlatlandDisplay::Name_)
            .AddRealmProtocol(fuchsia::ui::composition::Allocator::Name_)
            .AddRealmProtocol(fuchsia::ui::focus::FocusChainListenerRegistry::Name_)
            .Build());

    // Set up focus chain listener and wait for the initial null focus chain.
    fidl::InterfaceHandle<fuf_FocusChainListener> listener_handle;
    focus_chain_listener_.Bind(listener_handle.NewRequest());
    auto focus_chain_listener_registry = realm_->Connect<fuf_FocusChainListenerRegistry>();
    focus_chain_listener_registry->Register(std::move(listener_handle));
    EXPECT_EQ(CountReceivedFocusChains(), 0u);
    RunLoopUntil([this] { return CountReceivedFocusChains() == 1u; });
    EXPECT_FALSE(LastFocusChain()->has_focus_chain());

    scoped_observer_registry_ptr_ = realm_->Connect<fuos_Registry>();

    scoped_observer_registry_ptr_.set_error_handler([](zx_status_t status) {
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
    root_view_ref_koid_ = ExtractKoid(identity.view_ref);
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
    EXPECT_TRUE(LastFocusChain()->has_focus_chain());
    ASSERT_EQ(LastFocusChain()->focus_chain().size(), 1u);

    observed_focus_chains_.clear();
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

  const fuf_FocusChain* LastFocusChain() const {
    if (observed_focus_chains_.empty()) {
      return nullptr;
    } else {
      // Can't do std::optional<const FocusChain&>.
      return &observed_focus_chains_.back();
    }
  }

  const uint32_t kDefaultSize = 1;
  float display_width_ = 0;
  float display_height_ = 0;
  fuos_RegistryPtr scoped_observer_registry_ptr_;
  fuc_FlatlandPtr root_session_;
  zx_koid_t root_view_ref_koid_ = ZX_KOID_INVALID;
  fuv_FocuserPtr root_focuser_;
  std::unique_ptr<RealmRoot> realm_;

 private:
  fuc_FlatlandDisplayPtr flatland_display_;
  fidl::Binding<fuf_FocusChainListener> focus_chain_listener_;
  std::vector<fuf_FocusChain> observed_focus_chains_;
};

// Test fixture that sets up an environment with Registry protocol we can connect to. This test
// fixture is used for tests where the view nodes are created by GFX instances.
class GfxObserverRegistryIntegrationTest : public zxtest::Test,
                                           public loop_fixture::RealLoop,
                                           public fuf_FocusChainListener {
 protected:
  GfxObserverRegistryIntegrationTest() : focus_chain_listener_(this) {}
  fus_Scenic* scenic() { return scenic_.get(); }

  void SetUp() override {
    // Build the realm topology and route the protocols required by this test fixture from the
    // scenic subrealm.
    realm_ = std::make_unique<RealmRoot>(
        ScenicRealmBuilder({.use_flatland = false})
            .AddRealmProtocol(fuchsia::ui::observation::scope::Registry::Name_)
            .AddRealmProtocol(fuchsia::ui::scenic::Scenic::Name_)
            .AddRealmProtocol(fuchsia::ui::focus::FocusChainListenerRegistry::Name_)
            .Build());

    // Set up focus chain listener and wait for the initial null focus chain.
    fidl::InterfaceHandle<fuf_FocusChainListener> listener_handle;
    focus_chain_listener_.Bind(listener_handle.NewRequest());
    auto focus_chain_listener_registry = realm_->Connect<fuf_FocusChainListenerRegistry>();
    focus_chain_listener_registry->Register(std::move(listener_handle));
    EXPECT_EQ(CountReceivedFocusChains(), 0u);
    RunLoopUntil([this] { return CountReceivedFocusChains() == 1u; });
    EXPECT_FALSE(LastFocusChain()->has_focus_chain());

    scenic_ = realm_->Connect<fus_Scenic>();
    scenic_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
    });

    scoped_observer_registry_ptr_ = realm_->Connect<fuos_Registry>();
    scoped_observer_registry_ptr_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to Scoped Observer Registry Protocol: %s",
           zx_status_get_string(status));
    });

    // Set up root session.
    fuchsia::ui::scenic::SessionEndpoints endpoints;
    endpoints.set_view_focuser(root_focuser_.NewRequest());
    root_session_ =
        std::make_unique<GfxRootSession>(scenic(), GetDisplayDimensions(), std::move(endpoints));
    root_session_->session.set_error_handler([](zx_status_t status) {
      FAIL("Root session terminated: %s", zx_status_get_string(status));
    });

    display_height_ = root_session_->display_dimensions.height;
    display_width_ = root_session_->display_dimensions.width;
    BlockingPresent(root_session_->session);

    // Now that the scene exists, wait for a valid focus chain.  It should only contain the scene
    // node.
    RunLoopUntil([this] { return CountReceivedFocusChains() == 2u; });
    EXPECT_TRUE(LastFocusChain()->has_focus_chain());
    ASSERT_EQ(LastFocusChain()->focus_chain().size(), 1u);

    observed_focus_chains_.clear();
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

  // Blocking call to |fuchsia::ui::scenic::Scenic::GetDisplayInfo|.
  DisplayDimensions GetDisplayDimensions() {
    DisplayDimensions display_dimensions;
    scenic_->GetDisplayInfo([this, &display_dimensions](fug_DisplayInfo display_info) {
      display_dimensions = {.width = static_cast<float>(display_info.width_in_px),
                            .height = static_cast<float>(display_info.height_in_px)};
      QuitLoop();
    });
    RunLoop();
    return display_dimensions;
  }

  // Creates a red rectangle in the top left quadrant of the display.
  void CreateContent(scenic::Session& session, scenic::View& view, float width, float height) {
    scenic::Rectangle rec(&session, width / 2, height / 2);
    scenic::Material material(&session);
    std::array<uint8_t, 4> color = {255, 0, 0, 255};  // red
    material.SetColor(color[0], color[1], color[2], color[3]);

    scenic::ShapeNode shape(&session);

    shape.SetShape(rec);
    shape.SetMaterial(material);
    shape.SetTranslation(width / 4, height / 4, 0.f);

    view.AddChild(shape);
  }

  // Calculates view size based on view properties.
  fug_vec2 ViewSize(const fug_ViewProperties& view_properties) const {
    auto& p = view_properties;
    float size_x =
        ((p.bounding_box.max.x - p.inset_from_max.x) - (p.bounding_box.min.x + p.inset_from_min.x));
    float size_y =
        ((p.bounding_box.max.y - p.inset_from_max.y) - (p.bounding_box.min.y + p.inset_from_min.y));
    return fug_vec2{.x = size_x, .y = size_y};
  }

  // |fuchsia::ui::focus::FocusChainListener|
  void OnFocusChange(fuf_FocusChain focus_chain, OnFocusChangeCallback callback) override {
    observed_focus_chains_.push_back(std::move(focus_chain));
    callback();  // Receipt.
  }

  size_t CountReceivedFocusChains() const { return observed_focus_chains_.size(); }

  const fuf_FocusChain* LastFocusChain() const {
    if (observed_focus_chains_.empty()) {
      return nullptr;
    } else {
      // Can't do std::optional<const FocusChain&>.
      return &observed_focus_chains_.back();
    }
  }

  // Checks whether the view with |view_ref_koid| has connected to the view tree by checking its
  // presence in a Watch() call's response.
  bool HasViewConnected(const fuog_ViewTreeWatcherPtr& view_tree_watcher, zx_koid_t view_ref_koid) {
    std::optional<fuog_WatchResponse> view_tree_result;
    view_tree_watcher->Watch(
        [&view_tree_result](auto response) { view_tree_result = std::move(response); });
    RunLoopUntil([&view_tree_result] { return view_tree_result.has_value(); });
    auto snapshot = GetFirstSnapshotWithView(view_tree_result->updates(), view_ref_koid);
    return snapshot != view_tree_result->updates().end();
  }

  fuos_RegistryPtr scoped_observer_registry_ptr_;
  std::unique_ptr<GfxRootSession> root_session_;
  std::unique_ptr<RealmRoot> realm_;
  fuv_FocuserPtr root_focuser_;
  float display_width_ = 0.f;
  float display_height_ = 0.f;

 private:
  fus_ScenicPtr scenic_;
  fidl::Binding<fuf_FocusChainListener> focus_chain_listener_;
  std::vector<fuf_FocusChain> observed_focus_chains_;
};

// The client should receive updates whenever there is a change in the topology of the view tree.
// The view tree topology changes in the following manner in this test:
// root_view -> root_view    ->   root_view   ->  root_view
//                  |                 |               |
//            parent_view       parent_view     parent_view
//                                    |
//                               child_view
TEST_F(FlatlandObserverRegistryIntegrationTest, ClientReceivesTopologyUpdatesForFlatland) {
  fuog_ViewTreeWatcherPtr view_tree_watcher;

  // Set up the parent_view and connect it to the root_view.
  fuc_FlatlandPtr parent_session;
  zx_koid_t parent_view_ref_koid = ZX_KOID_INVALID;
  {
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    parent_session = realm_->Connect<fuc_Flatland>();
    fidl::InterfacePtr<fuc_ParentViewportWatcher> parent_viewport_watcher;
    fuc_ViewBoundProtocols protocols;
    auto identity = scenic::NewViewIdentityOnCreation();
    parent_view_ref_koid = ExtractKoid(identity.view_ref);

    ConnectChildView(root_session_, std::move(parent_token));

    parent_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                                parent_viewport_watcher.NewRequest());

    // Register view tree watcher before proceeding.
    // We can't register earlier, because we need to know `parent_view_ref_koid` to register an
    // observer scoped to the parent view.
    std::optional<bool> result;
    scoped_observer_registry_ptr_->RegisterScopedViewTreeWatcher(
        parent_view_ref_koid, view_tree_watcher.NewRequest(), [&result] { result = true; });
    RunLoopUntil([&result] { return result.has_value(); });
    EXPECT_TRUE(result.value());

    BlockingPresent(parent_session);
  }

  // Set up the child_view and connect it to the parent_view.
  fuc_FlatlandPtr child_session;
  zx_koid_t child_view_ref_koid = ZX_KOID_INVALID;
  {
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    child_session = realm_->Connect<fuc_Flatland>();
    fidl::InterfacePtr<fuc_ParentViewportWatcher> parent_viewport_watcher;
    fuc_ViewBoundProtocols protocols;
    auto identity = scenic::NewViewIdentityOnCreation();
    child_view_ref_koid = ExtractKoid(identity.view_ref);

    ConnectChildView(parent_session, std::move(parent_token));

    child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                               parent_viewport_watcher.NewRequest());

    BlockingPresent(child_session);
  }

  // Detach the child_view from the parent_view.
  child_session->ReleaseView();
  BlockingPresent(child_session);

  std::optional<fuog_WatchResponse> view_tree_result;

  view_tree_watcher->Watch(
      [&view_tree_result](auto response) { view_tree_result = std::move(response); });

  RunLoopUntil([&view_tree_result] { return view_tree_result.has_value(); });

  EXPECT_FALSE(view_tree_result->has_error());

  ASSERT_TRUE(view_tree_result->has_updates());

  // This snapshot captures the state of the view tree when parent_view gets connected to the
  // root_view. The child view had not yet connected at this point, so we only
  // expect to see the parent view.
  {
    auto snapshot_iter =
        GetFirstSnapshotWithView(view_tree_result->updates(), parent_view_ref_koid);
    ASSERT_TRUE(snapshot_iter != view_tree_result->updates().end());
    AssertViewTreeSnapshot(*snapshot_iter, ViewBuilder().AddView(parent_view_ref_koid, {}).Build());
  }

  // This snapshot captures the state of the view tree when child_view gets connected to the
  // parent_view.
  {
    auto snapshot_iter = GetFirstSnapshotWithView(view_tree_result->updates(), child_view_ref_koid);
    ASSERT_TRUE(snapshot_iter != view_tree_result->updates().end());
    AssertViewTreeSnapshot(*snapshot_iter, ViewBuilder()
                                               .AddView(parent_view_ref_koid, {child_view_ref_koid})
                                               .AddView(child_view_ref_koid, {})
                                               .Build());
  }

  // This snapshot captures the state of the view tree when child_view detaches from the
  // parent_view.
  {
    // Updates are reversed to find the snapshot having only the parent_view after the
    // child_view gets connected. This represents child_view getting disconnected.
    std::reverse(view_tree_result->mutable_updates()->begin(),
                 view_tree_result->mutable_updates()->end());
    auto snapshot_iter =
        GetFirstSnapshotWithView(view_tree_result->updates(), parent_view_ref_koid);
    ASSERT_TRUE(snapshot_iter != view_tree_result->updates().end());

    AssertViewTreeSnapshot(*snapshot_iter, ViewBuilder().AddView(parent_view_ref_koid, {}).Build());
  }
}

TEST_F(FlatlandObserverRegistryIntegrationTest, ClientReceivesLayoutUpdatesForFlatland) {
  fuog_ViewTreeWatcherPtr view_tree_watcher;

  // Set up the parent_view and connect it to the root_view.
  fuc_FlatlandPtr parent_session;
  zx_koid_t parent_view_ref_koid = ZX_KOID_INVALID;
  {
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    parent_session = realm_->Connect<fuc_Flatland>();
    fidl::InterfacePtr<fuc_ParentViewportWatcher> parent_viewport_watcher;
    fuc_ViewBoundProtocols protocols;
    auto identity = scenic::NewViewIdentityOnCreation();
    parent_view_ref_koid = ExtractKoid(identity.view_ref);

    ConnectChildView(root_session_, std::move(parent_token));

    parent_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                                parent_viewport_watcher.NewRequest());

    // Register view tree watcher before proceeding.
    // We can't register earlier, because we need to know `parent_view_ref_koid` to register an
    // observer scoped to the parent view.
    std::optional<bool> result;
    scoped_observer_registry_ptr_->RegisterScopedViewTreeWatcher(
        parent_view_ref_koid, view_tree_watcher.NewRequest(), [&result] { result = true; });
    RunLoopUntil([&result] { return result.has_value(); });
    EXPECT_TRUE(result.value());

    BlockingPresent(parent_session);
  }

  // Set up the child_view and connect it to the parent_view.
  fuc_FlatlandPtr child_session;
  zx_koid_t child_view_ref_koid = ZX_KOID_INVALID;
  {
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    child_session = realm_->Connect<fuc_Flatland>();
    fidl::InterfacePtr<fuc_ParentViewportWatcher> parent_viewport_watcher;
    fuc_ViewBoundProtocols protocols;
    auto identity = scenic::NewViewIdentityOnCreation();
    child_view_ref_koid = ExtractKoid(identity.view_ref);

    ConnectChildView(parent_session, std::move(parent_token));

    child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                               parent_viewport_watcher.NewRequest());

    BlockingPresent(child_session);
  }

  // Modify the Viewport properties of the child.
  fuc_ViewportProperties properties;
  const int32_t width = 100, height = 100;
  properties.set_logical_size({width, height});
  parent_session->SetViewportProperties({1}, std::move(properties));

  BlockingPresent(parent_session);

  std::optional<fuog_WatchResponse> view_tree_result;

  view_tree_watcher->Watch(
      [&view_tree_result](auto response) { view_tree_result = std::move(response); });

  RunLoopUntil([&view_tree_result] { return view_tree_result.has_value(); });

  EXPECT_FALSE(view_tree_result->has_error());

  ASSERT_TRUE(view_tree_result->has_updates());

  // This snapshot captures the state of the view tree when the parent view sets the logical size
  // of the viewport as {|kDefaultSize|,|kDefaultSize|}.
  {
    // The first snapshot having the child view should represent the state where the layout size of
    // the child view is {|width|,|height|}.
    auto snapshot_iter = GetFirstSnapshotWithView(view_tree_result->updates(), child_view_ref_koid);
    AssertViewTreeSnapshot(*snapshot_iter, ViewBuilder()
                                               .AddView(parent_view_ref_koid, {child_view_ref_koid},
                                                        std::make_pair(kDefaultSize, kDefaultSize))
                                               .AddView(child_view_ref_koid, {},
                                                        std::make_pair(kDefaultSize, kDefaultSize))
                                               .Build());
  }

  // This snapshot captures the state of the view tree when the parent view sets the logical size
  // of the viewport as {|width|,|height|}.
  {
    // The last snapshot having the child view should represent the state where the layout size of
    // the child view is {|kDefaultSize|,|kDefaultSize|}.
    std::reverse(view_tree_result->mutable_updates()->begin(),
                 view_tree_result->mutable_updates()->end());
    auto snapshot_iter = GetFirstSnapshotWithView(view_tree_result->updates(), child_view_ref_koid);
    AssertViewTreeSnapshot(*snapshot_iter,
                           ViewBuilder()
                               .AddView(parent_view_ref_koid, {child_view_ref_koid},
                                        std::make_pair(kDefaultSize, kDefaultSize))
                               .AddView(child_view_ref_koid, {}, std::make_pair(width, height))
                               .Build());
  }
}

// A view present in a fuog_ViewTreeSnapshot must be present in the view tree and should be
// focusable and hittable. In this test, the client (root view) uses |f.u.o.g.Provider| to get
// notified about a child view getting connected and then moves focus to the child view.
TEST_F(FlatlandObserverRegistryIntegrationTest, ChildRequestsFocusAfterConnectingForFlatland) {
  fuog_ViewTreeWatcherPtr view_tree_watcher;

  // Set up the child view and connect it to the root view.
  fuc_FlatlandPtr child_session;
  fuv_ViewRef child_view_ref;
  zx_koid_t child_view_ref_koid = ZX_KOID_INVALID;
  fuv_ViewRefFocusedPtr child_focused_ptr;
  {
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    child_session = realm_->Connect<fuc_Flatland>();
    fidl::InterfacePtr<fuc_ParentViewportWatcher> parent_viewport_watcher;
    fuc_ViewBoundProtocols protocols;
    protocols.set_view_ref_focused(child_focused_ptr.NewRequest());
    auto identity = scenic::NewViewIdentityOnCreation();
    child_view_ref = fidl::Clone(identity.view_ref);
    child_view_ref_koid = ExtractKoid(identity.view_ref);

    ConnectChildView(root_session_, std::move(parent_token));

    child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                               parent_viewport_watcher.NewRequest());

    // Register view tree watcher before proceeding.
    // We can't register earlier, because we need to know `child_view_ref_koid` to register an
    // observer scoped to the child view.
    std::optional<bool> result;
    scoped_observer_registry_ptr_->RegisterScopedViewTreeWatcher(
        child_view_ref_koid, view_tree_watcher.NewRequest(), [&result] { result = true; });
    RunLoopUntil([&result] { return result.has_value(); });
    EXPECT_TRUE(result.value());

    BlockingPresent(child_session);
  }

  // Watch for child focused event.
  std::optional<bool> child_focused;
  child_focused_ptr->Watch([&child_focused](auto update) {
    ASSERT_TRUE(update.has_focused());
    child_focused = update.focused();
  });

  std::optional<fuog_WatchResponse> view_tree_result;

  view_tree_watcher->Watch(
      [&view_tree_result](auto response) { view_tree_result = std::move(response); });

  RunLoopUntil([&view_tree_result] { return view_tree_result.has_value(); });

  ASSERT_TRUE(view_tree_result->has_updates());
  ASSERT_FALSE(view_tree_result->has_error());

  // Root view moves focus to the child view after it shows up in the fuog_ViewTreeSnapshot.
  std::optional<bool> request_processed;
  root_focuser_->RequestFocus(fidl::Clone(child_view_ref), [&request_processed](auto result) {
    request_processed = true;
    FX_DCHECK(!result.is_err());
  });

  RunLoopUntil([&request_processed, &child_focused] {
    return request_processed.has_value() && child_focused.has_value();
  });

  // This snapshot captures the state of the view tree when the child view gets connected to the
  // root view.
  auto snapshot = GetFirstSnapshotWithView(view_tree_result->updates(), child_view_ref_koid);
  ASSERT_TRUE(snapshot != view_tree_result->updates().end());

  // Child view should receive focus when it gets connected to the root view.
  EXPECT_TRUE(request_processed.value());
  EXPECT_TRUE(child_focused.value());
}

// The client should receive updates whenever there is a change in the topology of the view tree.
// The view tree topology changes in the following manner in this test:
// root_view -> root_view    ->   root_view   ->  root_view
//    (1)              |                 |               |
//            parent_view       parent_view     parent_view
//                  (2)                  |             (4)
//                               child_view
//                                    (3)
// Stage (1) and (2) are not reflected in the response because the child_view renders content in
// stage (3) after which its ancestors receive the |is_rendering| signal and are included in the
// response.
TEST_F(GfxObserverRegistryIntegrationTest, ClientReceivesHierarchyUpdatesForGfx) {
  fuog_ViewTreeWatcherPtr view_tree_watcher;

  // Set up the parent_view and connect it to the root_view.
  scenic::Session parent_session = CreateSession(scenic(), {});

  auto [parent_view_token, parent_view_holder_token] = scenic::ViewTokenPair::New();
  auto [parent_control_ref, parent_view_ref] = scenic::ViewRefPair::New();

  const auto parent_view_ref_koid = ExtractKoid(parent_view_ref);
  scenic::View parent_view(&parent_session, std::move(parent_view_token),
                           std::move(parent_control_ref), std::move(parent_view_ref),
                           "parent_view");

  scenic::EntityNode entity_node(&parent_session);
  entity_node.SetTranslation(0, 0, 0);
  parent_view.AddChild(entity_node);

  BlockingPresent(parent_session);

  // Register view tree watcher before proceeding.
  // We can't register earlier, because we need to know `parent_view_ref_koid` to register an
  // observer scoped to the parent view.
  std::optional<bool> result;
  scoped_observer_registry_ptr_->RegisterScopedViewTreeWatcher(
      parent_view_ref_koid, view_tree_watcher.NewRequest(), [&result] { result = true; });
  RunLoopUntil([&result] { return result.has_value(); });
  EXPECT_TRUE(result.value());

  scenic::ViewHolder parent_view_holder(&root_session_->session,
                                        std::move(parent_view_holder_token), "parent_holder");

  const std::array<float, 3> bmin = {0.f, 0.f, -2.f};
  const std::array<float, 3> bmax = {display_width_, display_height_, 1.f};
  const std::array<float, 3> imin = {0, 0, 0};
  const std::array<float, 3> imax = {0, 0, 0};
  parent_view_holder.SetViewProperties(bmin, bmax, imin, imax);
  parent_view_holder.SetTranslation(0, display_height_ / 2, 0);

  root_session_->scene.AddChild(parent_view_holder);

  BlockingPresent(root_session_->session);

  // parent_view is not present in the response until the child_view renders some content.
  EXPECT_FALSE(HasViewConnected(view_tree_watcher, parent_view_ref_koid));

  // Set up the child_view and connect it to the parent_view.
  scenic::Session child_session = CreateSession(scenic(), {});

  auto [child_view_token, child_view_holder_token] = scenic::ViewTokenPair::New();
  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();

  const auto child_view_ref_koid = ExtractKoid(child_view_ref);
  scenic::View child_view(&child_session, std::move(child_view_token), std::move(child_control_ref),
                          std::move(child_view_ref), "child_view");

  scenic::ViewHolder child_view_holder(&parent_session, std::move(child_view_holder_token),
                                       "child_holder");

  entity_node.AddChild(child_view_holder);
  BlockingPresent(child_session);
  BlockingPresent(parent_session);

  // child_view renders some content which generates the |is_rendering| signal.
  CreateContent(child_session, child_view, display_width_, display_height_);

  BlockingPresent(child_session);
  BlockingPresent(parent_session);

  std::optional<fuog_WatchResponse> view_tree_result;

  view_tree_watcher->Watch(
      [&view_tree_result](auto response) { view_tree_result = std::move(response); });

  RunLoopUntil([&view_tree_result] { return view_tree_result.has_value(); });

  EXPECT_FALSE(view_tree_result->has_error());

  ASSERT_TRUE(view_tree_result->has_updates());

  // This snapshot captures the state of the view tree when child_view gets connected to the
  // parent_view. Note that all the views will be present in the response only when the child_view
  // has rendered some content.
  {
    auto snapshot_iter = GetFirstSnapshotWithView(view_tree_result->updates(), child_view_ref_koid);
    ASSERT_TRUE(snapshot_iter != view_tree_result->updates().end());

    AssertViewTreeSnapshot(*snapshot_iter, ViewBuilder()
                                               .AddView(parent_view_ref_koid, {child_view_ref_koid})
                                               .AddView(child_view_ref_koid, {})
                                               .Build());
  }

  // Detach the child_view from the parent_view.
  parent_view.DetachChild(entity_node);
  BlockingPresent(parent_session);

  view_tree_result.reset();

  view_tree_watcher->Watch(
      [&view_tree_result](auto response) { view_tree_result = std::move(response); });

  RunLoopUntil([&view_tree_result] { return view_tree_result.has_value(); });

  EXPECT_FALSE(view_tree_result->has_error());

  ASSERT_TRUE(view_tree_result->has_updates());

  // This snapshot captures the state of the view tree when child_view detaches from the
  // parent_view.
  {
    auto snapshot_iter =
        GetFirstSnapshotWithView(view_tree_result->updates(), parent_view_ref_koid);
    ASSERT_TRUE(snapshot_iter != view_tree_result->updates().end());
    AssertViewTreeSnapshot(*snapshot_iter, ViewBuilder().AddView(parent_view_ref_koid, {}).Build());
  }
}

// A view present in a fuog_ViewTreeSnapshot must be present in the view tree and should be
// focusable and hittable. In this test, the client (root view) uses |f.u.o.g.Provider| to get
// notified about a child view getting connected and then moves focus to the child view.
TEST_F(GfxObserverRegistryIntegrationTest, ChildRequestsFocusAfterConnectingForGFX) {
  fuog_ViewTreeWatcherPtr view_tree_watcher;

  scenic::EntityNode entity_node(&root_session_->session);

  fus_SessionEndpoints endpoints;
  fuv_ViewRefFocusedPtr child_focused_ptr;
  endpoints.set_view_ref_focused(child_focused_ptr.NewRequest());
  auto child_session = CreateSession(scenic(), std::move(endpoints));

  std::optional<fug_ViewProperties> view_properties;
  child_session.set_event_handler([&view_properties](const std::vector<fus_Event>& events) {
    for (const auto& event : events) {
      if (!event.is_gfx())
        continue;  // skip non-gfx events
      if (event.gfx().is_view_properties_changed()) {
        view_properties = event.gfx().view_properties_changed().properties;
      }
    }
  });

  // Setup a view and connect it to the root_view.
  auto [child_view_token, child_view_holder_token] = scenic::ViewTokenPair::New();
  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();

  const auto child_view_ref_koid = ExtractKoid(child_view_ref);
  fuv_ViewRef child_view_ref_copy;
  fidl::Clone(child_view_ref, &child_view_ref_copy);
  scenic::View child_view(&child_session, std::move(child_view_token), std::move(child_control_ref),
                          std::move(child_view_ref), "child_view");

  scenic::ViewHolder child_view_holder(&root_session_->session, std::move(child_view_holder_token),
                                       "child_view_holder");

  const std::array<float, 3> bmin = {0.f, 0.f, -2.f};
  const std::array<float, 3> bmax = {display_width_, display_height_, 1.f};
  const std::array<float, 3> imin = {0, 0, 0};
  const std::array<float, 3> imax = {0, 0, 0};
  child_view_holder.SetViewProperties(bmin, bmax, imin, imax);

  entity_node.AddChild(child_view_holder);
  root_session_->scene.AddChild(entity_node);

  child_session.Present2(0, 0, [](auto) {});
  root_session_->session.Present2(0, 0, [](auto) {});

  // Register view tree watcher before proceeding.
  // We can't register earlier, because we need to know `child_view_ref_koid` to register an
  // observer scoped to the child view.
  std::optional<bool> result;
  scoped_observer_registry_ptr_->RegisterScopedViewTreeWatcher(
      child_view_ref_koid, view_tree_watcher.NewRequest(), [&result] { result = true; });
  RunLoopUntil([&result] { return result.has_value(); });
  EXPECT_TRUE(result.value());

  // The view is not included in the response because it has not rendered any content.
  EXPECT_FALSE(HasViewConnected(view_tree_watcher, child_view_ref_koid));
  FX_LOGS(INFO) << "HasViewConnected completed";
  // Watch for child focused event.
  std::optional<bool> child_focused;
  child_focused_ptr->Watch([&child_focused](auto update) {
    ASSERT_TRUE(update.has_focused());
    child_focused = update.focused();
  });

  // Use the |view_properties| received from the root_view to create a rectangle on the screen.
  RunLoopUntil([&view_properties] { return view_properties.has_value(); });
  FX_LOGS(INFO) << "Received view properties";
  CreateContent(child_session, child_view, ViewSize(*view_properties).x,
                ViewSize(*view_properties).y);

  child_session.Present2(0, 0, [](auto) {});
  root_session_->session.Present2(0, 0, [](auto) {});

  // Wait until the child_view is present in the Watch() call's response.
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [this, &view_tree_watcher, &child_view_ref_koid] {
        return HasViewConnected(view_tree_watcher, child_view_ref_koid);
      },
      kWatchTimeout));
  FX_LOGS(INFO) << "View connected";

  // Root view moves focus to the child view after it shows up in the fuog_ViewTreeSnapshot.
  std::optional<bool> request_processed;
  root_focuser_->RequestFocus(fidl::Clone(child_view_ref_copy), [&request_processed](auto result) {
    request_processed = true;
    FX_DCHECK(!result.is_err());
  });

  RunLoopUntil([&request_processed, &child_focused] {
    return request_processed.has_value() && child_focused.has_value();
  });
  FX_LOGS(INFO) << "RequestFocus Complete";

  // Child view should receive focus when it gets connected to the root view.
  EXPECT_TRUE(request_processed.value());
  EXPECT_TRUE(child_focused.value());
}

// This test ensures that the client receives a view's inset and scale attributes when
// they change.
TEST_F(GfxObserverRegistryIntegrationTest, GFXClientReceivesPixelScaleAndInsetValues) {
  fuog_ViewTreeWatcherPtr view_tree_watcher;

  scenic::EntityNode entity_node(&root_session_->session);

  auto child_session = CreateSession(scenic(), {});

  std::optional<fug_ViewProperties> view_properties;
  child_session.set_event_handler([&view_properties](const std::vector<fus_Event>& events) {
    for (const auto& event : events) {
      if (!event.is_gfx())
        continue;  // skip non-gfx events
      if (event.gfx().is_view_properties_changed()) {
        view_properties = event.gfx().view_properties_changed().properties;
      }
    }
  });

  // Setup a view and connect it to the root_view.
  auto [child_view_token, child_view_holder_token] = scenic::ViewTokenPair::New();
  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();

  const auto child_view_ref_koid = ExtractKoid(child_view_ref);
  scenic::View child_view(&child_session, std::move(child_view_token), std::move(child_control_ref),
                          std::move(child_view_ref), "child_view");

  scenic::ViewHolder child_view_holder(&root_session_->session, std::move(child_view_holder_token),
                                       "child_view_holder");

  // Setup the view properties and scale for the view.
  const std::array<float, 3> bmin = {0.f, 0.f, -2.f};
  const std::array<float, 3> bmax = {display_width_, display_height_, 1.f};
  const std::array<float, 3> inset_min = {2, 2, 2};
  const std::array<float, 3> inset_max = {2, 2, 2};
  const std::array<float, 3> scale = {2, 2, 2};
  child_view_holder.SetViewProperties(bmin, bmax, inset_min, inset_max);
  child_view_holder.SetEventMask(fuchsia::ui::gfx::kMetricsEventMask);
  child_view_holder.SetScale(scale[0], scale[1], scale[2]);

  entity_node.AddChild(child_view_holder);
  root_session_->scene.AddChild(entity_node);

  BlockingPresent(child_session);
  BlockingPresent(root_session_->session);

  // Register view tree watcher before proceeding.
  // We can't register earlier, because we need to know `child_view_ref_koid` to register an
  // observer scoped to the child view.
  std::optional<bool> result;
  scoped_observer_registry_ptr_->RegisterScopedViewTreeWatcher(
      child_view_ref_koid, view_tree_watcher.NewRequest(), [&result] { result = true; });
  RunLoopUntil([&result] { return result.has_value(); });
  EXPECT_TRUE(result.value());

  // Use the |view_properties| received from the root_view to create a rectangle on the screen.
  RunLoopUntil([&view_properties] { return view_properties.has_value(); });
  CreateContent(child_session, child_view, ViewSize(*view_properties).x,
                ViewSize(*view_properties).y);

  BlockingPresent(child_session);
  BlockingPresent(root_session_->session);

  std::optional<fuog_WatchResponse> view_tree_result;

  view_tree_watcher->Watch(
      [&view_tree_result](auto response) { view_tree_result = std::move(response); });

  RunLoopUntil([&view_tree_result] { return view_tree_result.has_value(); });

  EXPECT_FALSE(view_tree_result->has_error());

  ASSERT_TRUE(view_tree_result->has_updates());

  // This snapshot captures the state of the view tree when child view gets connected to the
  // root view.
  {
    auto snapshot_iter = GetFirstSnapshotWithView(view_tree_result->updates(), child_view_ref_koid);
    ASSERT_TRUE(snapshot_iter != view_tree_result->updates().end());

    // fuog_Provider captures pixel_scale and inset information for the child view.
    auto& child_vd = snapshot_iter->views()[0];
    EXPECT_TRUE(CmpFloatingValues(child_vd.layout().pixel_scale[0], scale[0]));
    EXPECT_TRUE(CmpFloatingValues(child_vd.layout().pixel_scale[1], scale[1]));

    EXPECT_TRUE(CmpFloatingValues(child_vd.layout().inset.top, inset_min[1]));
    EXPECT_TRUE(CmpFloatingValues(child_vd.layout().inset.left, inset_min[0]));
    EXPECT_TRUE(CmpFloatingValues(child_vd.layout().inset.bottom, inset_max[1]));
    EXPECT_TRUE(CmpFloatingValues(child_vd.layout().inset.right, inset_max[0]));
  }
}

}  // namespace integration_tests
