// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/tests/semantics_integration_test_fixture_v2.h"

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/fonts/cpp/fidl.h>
#include <fuchsia/hardware/display/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/kernel/cpp/fidl.h>
#include <fuchsia/memorypressure/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/posix/socket/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "fuchsia/sysmem/cpp/fidl.h"
#include "src/lib/fsl/handles/object_info.h"

namespace accessibility_test {

using ScenicEvent = fuchsia::ui::scenic::Event;
using GfxEvent = fuchsia::ui::gfx::Event;

using fuchsia::accessibility::semantics::Node;
using sys::testing::AboveRoot;
using sys::testing::LegacyComponentUrl;
using sys::testing::Mock;
using sys::testing::Protocol;

void AddBaseComponents(RealmBuilder* realm_builder) {
  FX_LOGS(INFO) << "Add root presenter";
  realm_builder->AddComponent(
      SemanticsIntegrationTestV2::kRootPresenterMoniker,
      Component{
          .source = LegacyComponentUrl{
              "fuchsia-pkg://fuchsia.com/semantics-integration-tests#meta/root_presenter.cmx"}});
  FX_LOGS(INFO) << "Add scenic";
  realm_builder->AddComponent(
      SemanticsIntegrationTestV2::kScenicMoniker,
      Component{.source = LegacyComponentUrl{"fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx"}});
  FX_LOGS(INFO) << "Add cobalt";
  realm_builder->AddComponent(
      SemanticsIntegrationTestV2::kMockCobaltMoniker,
      Component{.source = LegacyComponentUrl{
                    "fuchsia-pkg://fuchsia.com/mock_cobalt#meta/mock_cobalt.cmx"}});
  FX_LOGS(INFO) << "Add hdcp";
  realm_builder->AddComponent(
      SemanticsIntegrationTestV2::kHdcpMoniker,
      Component{.source =
                    LegacyComponentUrl{"fuchsia-pkg://fuchsia.com/"
                                       "fake-hardware-display-controller-provider#meta/hdcp.cmx"}});
}

void AddBaseRoutes(RealmBuilder* realm_builder) {
  // Capabilities routed from test_manager to components in realm.
  FX_LOGS(INFO) << "Add fuchsia::vulkan::loader::Loader";
  realm_builder->AddRoute(
      CapabilityRoute{.capability = Protocol{fuchsia::vulkan::loader::Loader::Name_},
                      .source = AboveRoot(),
                      .targets = {SemanticsIntegrationTestV2::kScenicMoniker}});
  FX_LOGS(INFO) << "Add fuchsia::scheduler::ProfileProvider";
  realm_builder->AddRoute(
      CapabilityRoute{.capability = Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                      .source = AboveRoot(),
                      .targets = {SemanticsIntegrationTestV2::kScenicMoniker}});
  FX_LOGS(INFO) << "Add fuchsia::sysmem::Allocator";
  realm_builder->AddRoute(CapabilityRoute{.capability = Protocol{fuchsia::sysmem::Allocator::Name_},
                                          .source = AboveRoot(),
                                          .targets = {SemanticsIntegrationTestV2::kScenicMoniker,
                                                      SemanticsIntegrationTestV2::kHdcpMoniker}});
  FX_LOGS(INFO) << "Add fuchsia::tracing::provider::Registry";
  realm_builder->AddRoute(
      CapabilityRoute{.capability = Protocol{fuchsia::tracing::provider::Registry::Name_},
                      .source = AboveRoot(),
                      .targets = {SemanticsIntegrationTestV2::kScenicMoniker,
                                  SemanticsIntegrationTestV2::kRootPresenterMoniker,
                                  SemanticsIntegrationTestV2::kHdcpMoniker}});

  // Capabilities routed between siblings in realm.
  FX_LOGS(INFO) << "Add fuchsia::cobalt::LoggerFactory";
  realm_builder->AddRoute(
      CapabilityRoute{.capability = Protocol{fuchsia::cobalt::LoggerFactory::Name_},
                      .source = SemanticsIntegrationTestV2::kMockCobaltMoniker,
                      .targets = {SemanticsIntegrationTestV2::kScenicMoniker}});
  FX_LOGS(INFO) << "Add fuchsia::hardware::display::Provider";
  realm_builder->AddRoute(
      CapabilityRoute{.capability = Protocol{fuchsia::hardware::display::Provider::Name_},
                      .source = SemanticsIntegrationTestV2::kHdcpMoniker,
                      .targets = {SemanticsIntegrationTestV2::kScenicMoniker}});
  FX_LOGS(INFO) << "Add fuchsia::ui::scenic::Scenic";
  realm_builder->AddRoute(
      CapabilityRoute{.capability = Protocol{fuchsia::ui::scenic::Scenic::Name_},
                      .source = SemanticsIntegrationTestV2::kScenicMoniker,
                      .targets = {SemanticsIntegrationTestV2::kRootPresenterMoniker}});
  FX_LOGS(INFO) << "Add fuchsia::ui::pointerinjector::Registry";
  realm_builder->AddRoute(
      CapabilityRoute{.capability = Protocol{fuchsia::ui::pointerinjector::Registry::Name_},
                      .source = SemanticsIntegrationTestV2::kScenicMoniker,
                      .targets = {SemanticsIntegrationTestV2::kRootPresenterMoniker}});

  // Capabilities routed up to test driver (this component).
  FX_LOGS(INFO) << "Add fuchsia::ui::policy::Presenter";
  realm_builder->AddRoute(
      CapabilityRoute{.capability = Protocol{fuchsia::ui::policy::Presenter::Name_},
                      .source = SemanticsIntegrationTestV2::kRootPresenterMoniker,
                      .targets = {AboveRoot()}});
  FX_LOGS(INFO) << "Add fuchsia::ui::scenic::Scenic";
  realm_builder->AddRoute(
      CapabilityRoute{.capability = Protocol{fuchsia::ui::scenic::Scenic::Name_},
                      .source = SemanticsIntegrationTestV2::kScenicMoniker,
                      .targets = {AboveRoot()}});
}

void SemanticsManagerProxy::Start(std::unique_ptr<MockHandles> mock_handles) {
  FX_CHECK(mock_handles->outgoing()->AddPublicService(
               fidl::InterfaceRequestHandler<fuchsia::accessibility::semantics::SemanticsManager>(
                   [this](auto request) {
                     bindings_.AddBinding(this, std::move(request), dispatcher_);
                   })) == ZX_OK);
  mock_handles_.emplace_back(std::move(mock_handles));
}

void SemanticsManagerProxy::RegisterViewForSemantics(
    fuchsia::ui::views::ViewRef view_ref,
    fidl::InterfaceHandle<fuchsia::accessibility::semantics::SemanticListener> handle,
    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request) {
  semantics_manager_->RegisterViewForSemantics(std::move(view_ref), std::move(handle),
                                               std::move(semantic_tree_request));
}

void SemanticsIntegrationTestV2::SetUp() {
  view_manager_ = std::make_unique<a11y::ViewManager>(
      std::make_unique<a11y::SemanticTreeServiceFactory>(),
      std::make_unique<MockViewSemanticsFactory>(), std::make_unique<MockAnnotationViewFactory>(),
      std::make_unique<MockViewInjectorFactory>(),
      std::make_unique<a11y::A11ySemanticsEventManager>(),
      std::make_unique<MockAccessibilityView>(), context_.get(), context_->outgoing()->debug_dir());

  BuildRealm(this->GetTestComponents(), this->GetTestRoutes());

  // Wait until scenic is initialized to continue.
  scenic_ = realm()->Connect<fuchsia::ui::scenic::Scenic>();
  scenic_->GetDisplayInfo([this](fuchsia::ui::gfx::DisplayInfo display_info) { QuitLoop(); });
  RunLoop();
}

void SemanticsIntegrationTestV2::LaunchClient(std::string debug_name) {
  auto tokens_rt = scenic::ViewTokenPair::New();  // Root Presenter -> Test
  auto tokens_tf = scenic::ViewTokenPair::New();  // Test -> Client

  // Instruct Root Presenter to present test's View.
  auto root_presenter = realm()->Connect<fuchsia::ui::policy::Presenter>();
  root_presenter->PresentOrReplaceView(std::move(tokens_rt.view_holder_token),
                                       /* presentation */ nullptr);

  // Set up test's View, to harvest the client view's view_state.is_rendering signal.
  auto session_pair = scenic::CreateScenicSessionPtrAndListenerRequest(scenic_.get());
  session_ = std::make_unique<scenic::Session>(std::move(session_pair.first),
                                               std::move(session_pair.second));
  session_->SetDebugName(debug_name);
  bool is_rendering = false;
  session_->set_event_handler(
      [this, debug_name, &is_rendering](const std::vector<fuchsia::ui::scenic::Event>& events) {
        for (const auto& event : events) {
          if (!event.is_gfx())
            continue;  // skip non-gfx events

          if (event.gfx().is_view_properties_changed()) {
            const auto properties = event.gfx().view_properties_changed().properties;
            FX_CHECK(view_holder_) << "Expect that view holder is already set up.";
            view_holder_->SetViewProperties(properties);
            session_->Present2(/*when*/ zx::clock::get_monotonic().get(), /*span*/ 0, [](auto) {});

          } else if (event.gfx().is_view_state_changed()) {
            is_rendering = event.gfx().view_state_changed().state.is_rendering;
            FX_VLOGS(1) << "Child's view content is rendering: " << std::boolalpha << is_rendering;
          }
        }
      });

  view_holder_ = std::make_unique<scenic::ViewHolder>(
      session_.get(), std::move(tokens_tf.view_holder_token), "test's view holder");
  view_ = std::make_unique<scenic::View>(session_.get(), std::move(tokens_rt.view_token),
                                         "test's view");
  view_->AddChild(*view_holder_);

  // Request to make test's view; this will trigger dispatch of view properties.
  session_->Present2(/*when*/ zx::clock::get_monotonic().get(), /*span*/ 0,
                     [](auto) { FX_VLOGS(1) << "test's view and view holder created by Scenic."; });

  // Start client app inside the test environment.
  // Note well. There is a significant difference in how ViewProvider is
  // vended and used, between CF v1 and CF v2. This test follows the CF v2
  // style: the realm specifies a component C that can serve ViewProvider, and
  // when the test runner asks for that protocol, C is launched by Component
  // Manager. In contrast, production uses CF v1 style, where a parent
  // component P launches a child component C directly, and P connects to C's
  // ViewProvider directly. However, this difference does not impact the
  // testing logic.
  auto view_provider = realm()->Connect<fuchsia::ui::app::ViewProvider>();
  auto [client_control_ref, client_view_ref] = scenic::ViewRefPair::New();
  view_ref_koid_ = fsl::GetKoid(client_view_ref.reference.get());
  view_provider->CreateViewWithViewRef(std::move(tokens_tf.view_token.value),
                                       std::move(client_control_ref), std::move(client_view_ref));

  RunLoopUntil([&is_rendering] { return is_rendering; });

  // Reset the event handler without capturing the is_rendering stack variable.
  session_->set_event_handler(
      [this, debug_name](const std::vector<fuchsia::ui::scenic::Event>& events) {
        for (const auto& event : events) {
          if (!event.is_gfx())
            continue;  // skip non-gfx events

          if (event.gfx().is_view_properties_changed()) {
            const auto properties = event.gfx().view_properties_changed().properties;
            FX_CHECK(view_holder_) << "Expect that view holder is already set up.";
            view_holder_->SetViewProperties(properties);
            session_->Present2(/*when*/ zx::clock::get_monotonic().get(), /*span*/ 0, [](auto) {});
          }
        }
      });
}

void SemanticsIntegrationTestV2::BuildRealm(
    const std::vector<std::pair<Moniker, Component>>& components,
    const std::vector<CapabilityRoute>& routes) {
  semantics_manager_proxy_ = std::make_unique<SemanticsManagerProxy>(view_manager(), dispatcher());
  builder()->AddComponent(SemanticsIntegrationTestV2::kSemanticsManagerMoniker,
                          Component{.source = Mock{semantics_manager_proxy()}});

  // Add all components shared by each test to the realm.
  AddBaseComponents(builder());

  // Add components specific for this test case to the realm.
  for (const auto& [moniker, component] : components) {
    builder()->AddComponent(moniker, component);
  }

  // Add the necessary routing for each of the base components added above.
  AddBaseRoutes(builder());

  // Add the necessary routing for each of the extra components added above.
  for (const auto& route : routes) {
    builder()->AddRoute(route);
  }

  // Finally, build the realm using the provided components and routes.
  realm_ = std::make_unique<Realm>(builder()->Build());
}

const Node* SemanticsIntegrationTestV2::FindNodeWithLabel(const Node* node, zx_koid_t view_ref_koid,
                                                          std::string label) {
  if (!node) {
    return nullptr;
  }

  if (node->has_attributes() && node->attributes().has_label() &&
      node->attributes().label() == label) {
    return node;
  }

  if (!node->has_child_ids()) {
    return nullptr;
  }
  for (const auto& child_id : node->child_ids()) {
    const auto* child = view_manager()->GetSemanticNode(view_ref_koid, child_id);
    FX_DCHECK(child);
    auto result = FindNodeWithLabel(child, view_ref_koid, label);
    if (result != nullptr) {
      return result;
    }
  }

  return nullptr;
}

a11y::SemanticTransform SemanticsIntegrationTestV2::GetTransformForNode(zx_koid_t view_ref_koid,
                                                                        uint32_t node_id) {
  std::vector<const Node*> path;
  // Perform a DFS to find the path to the target node
  std::function<bool(const Node*)> traverse = [&](const Node* node) {
    if (node->node_id() == node_id) {
      path.push_back(node);
      return true;
    }
    if (!node->has_child_ids()) {
      return false;
    }
    for (const auto& child_id : node->child_ids()) {
      const auto* child = view_manager()->GetSemanticNode(view_ref_koid, child_id);
      FX_DCHECK(child);
      if (traverse(child)) {
        path.push_back(node);
        return true;
      }
    }
    return false;
  };

  auto root = view_manager()->GetSemanticNode(view_ref_koid, 0u);
  traverse(root);

  a11y::SemanticTransform transform;
  for (auto& node : path) {
    if (node->has_transform()) {
      transform.ChainLocalTransform(node->transform());
    }
  }

  return transform;
}

std::optional<uint32_t> SemanticsIntegrationTestV2::HitTest(zx_koid_t view_ref_koid,
                                                            fuchsia::math::PointF target) {
  std::optional<fuchsia::accessibility::semantics::Hit> target_hit;
  FX_LOGS(INFO) << "target is: " << target.x << ":" << target.y;
  auto hit_callback = [&target_hit](fuchsia::accessibility::semantics::Hit hit) {
    target_hit = std::move(hit);
  };

  view_manager()->ExecuteHitTesting(view_ref_koid, target, hit_callback);

  RunLoopUntil([&target_hit] { return target_hit.has_value(); });
  if (!target_hit.has_value() || !target_hit->has_node_id()) {
    return std::nullopt;
  }
  return target_hit->node_id();
}

fuchsia::math::PointF
SemanticsIntegrationTestV2::CalculateCenterOfSemanticNodeBoundingBoxCoordinate(
    zx_koid_t view_ref_koid, const fuchsia::accessibility::semantics::Node* node) {
  // Semantic trees may have transforms in each node.  That transform defines the spatial relation
  // between coordinates in the node's space to coordinates in it's parent's space.  This is done
  // to enable semantic providers to avoid recomputing location information on every child node
  // when a parent node (or the entire view) undergoes a spatial change.

  // Get the transform from the node's local space to the view's local space.
  auto transform = view_manager()->GetNodeToRootTransform(view_ref_koid, node->node_id());
  FX_DCHECK(transform) << "Could not compute a transform for the semantic node: " << view_ref_koid
                       << ":" << node->node_id();

  const auto node_bounding_box = node->location();
  const auto node_bounding_box_center_x = (node_bounding_box.min.x + node_bounding_box.max.x) / 2.f;
  const auto node_bounding_box_center_y = (node_bounding_box.min.y + node_bounding_box.max.y) / 2.f;
  const fuchsia::ui::gfx::vec3 node_bounding_box_center_local = {node_bounding_box_center_x,
                                                                 node_bounding_box_center_y, 0.f};

  const fuchsia::ui::gfx::vec3 node_bounding_box_center_root =
      transform->Apply(node_bounding_box_center_local);

  return {node_bounding_box_center_root.x, node_bounding_box_center_root.y};
}

bool SemanticsIntegrationTestV2::PerformAccessibilityAction(
    zx_koid_t view_ref_koid, uint32_t node_id, fuchsia::accessibility::semantics::Action action) {
  std::optional<bool> callback_handled;
  auto callback = [&callback_handled](bool handled) { callback_handled = handled; };
  view_manager()->PerformAccessibilityAction(view_ref_koid, node_id, action, callback);

  RunLoopUntil([&callback_handled] { return callback_handled.has_value(); });
  return *callback_handled;
}
}  // namespace accessibility_test
