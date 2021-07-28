// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/tests/semantics_integration_test_fixture.h"

#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/fdio/spawn.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <stack>
#include <vector>

#include "src/ui/a11y/lib/annotation/tests/mocks/mock_annotation_view.h"
#include "src/ui/a11y/lib/semantics/a11y_semantics_event_manager.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_accessibility_view.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_injector_factory.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_semantics.h"

namespace accessibility_test {

namespace {

// Services to inject into the test environment that we want to re-create for each test case
constexpr size_t kNumInjectedServices = 15;
constexpr std::array<std::pair<const char*, const char*>, kNumInjectedServices> kInjectedServices =
    {{
        // clang-format off
    {
      "fuchsia.accessibility.ColorTransform",
      "fuchsia-pkg://fuchsia.com/a11y-manager#meta/a11y-manager.cmx"
    }, {
      "fuchsia.accessibility.Magnifier",
      "fuchsia-pkg://fuchsia.com/a11y-manager#meta/a11y-manager.cmx"
    }, {
      "fuchsia.feedback.LastRebootInfoProvider",
      "fuchsia-pkg://fuchsia.com/fake-last-reboot-info-provider#meta/fake_last_reboot_info_provider.cmx"
    }, {
      "fuchsia.fonts.Provider",
      "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx"
    }, {
      "fuchsia.hardware.display.Provider",
      "fuchsia-pkg://fuchsia.com/fake-hardware-display-controller-provider#meta/hdcp.cmx"
    }, {
      "fuchsia.intl.PropertyProvider",
      "fuchsia-pkg://fuchsia.com/intl-services-small#meta/intl_services.cmx"
    }, {
      "fuchsia.settings.Intl",
      "fuchsia-pkg://fuchsia.com/setui_service#meta/setui_service.cmx"
    }, {
      "fuchsia.stash.Store",
      "fuchsia-pkg://fuchsia.com/stash#meta/stash.cmx"
    }, {
      "fuchsia.ui.input.accessibility.PointerEventRegistry",
      "fuchsia-pkg://fuchsia.com/root_presenter#meta/root_presenter.cmx"
    }, {
      "fuchsia.ui.input.ImeService",
      "fuchsia-pkg://fuchsia.com/ime_service#meta/ime_service.cmx"
    }, {
      "fuchsia.ui.input.ImeVisibilityService",
      "fuchsia-pkg://fuchsia.com/ime_service#meta/ime_service.cmx"
    }, {
      "fuchsia.ui.pointerinjector.Registry",
      "fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx"
    }, {
      "fuchsia.ui.policy.accessibility.PointerEventRegistry",
      "fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx"
    }, {
      "fuchsia.ui.scenic.Scenic",
      "fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx"
    }, {
      "fuchsia.ui.policy.Presenter",
      "fuchsia-pkg://fuchsia.com/root_presenter#meta/root_presenter.cmx"
    },
        // clang-format on
    }};

}  // namespace

SemanticsIntegrationTest::SemanticsIntegrationTest(const std::string& environment_label)
    : environment_label_(environment_label),
      component_context_provider_(),
      view_manager_(std::make_unique<a11y::SemanticTreeServiceFactory>(),
                    std::make_unique<MockViewSemanticsFactory>(),
                    std::make_unique<MockAnnotationViewFactory>(),
                    std::make_unique<MockViewInjectorFactory>(),
                    std::make_unique<a11y::A11ySemanticsEventManager>(),
                    std::make_unique<MockAccessibilityView>(),
                    component_context_provider_.context(),
                    component_context_provider_.context()->outgoing()->debug_dir()) {}

void SemanticsIntegrationTest::SetUp() {
  TestWithEnvironment::SetUp();

  // This is done in |SetUp| as opposed to the constructor to allow subclasses the opportunity to
  // override |CreateServices()|.
  auto services = TestWithEnvironment::CreateServices();
  services->AddService(semantics_manager_bindings_.GetHandler(&view_manager_));

  // Add test-specific launchable services.
  for (const auto& service_info : kInjectedServices) {
    zx_status_t status =
        services->AddServiceWithLaunchInfo({.url = service_info.second}, service_info.first);
    ASSERT_EQ(status, ZX_OK) << service_info.first;
  }

  services->AllowParentService("fuchsia.logger.LogSink");
  services->AllowParentService("fuchsia.posix.socket.Provider");
  services->AllowParentService("fuchsia.tracing.provider.Registry");

  CreateServices(services);

  environment_ = CreateNewEnclosingEnvironment(environment_label_, std::move(services),
                                               {.inherit_parent_services = true});
  WaitForEnclosingEnvToStart(environment_.get());

  scenic_ = environment()->ConnectToService<fuchsia::ui::scenic::Scenic>();
  scenic_.set_error_handler([](zx_status_t status) {
    FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
  });

  // Wait for scenic to get initialized by calling GetDisplayInfo.
  scenic_->GetDisplayInfo([this](fuchsia::ui::gfx::DisplayInfo info) { QuitLoop(); });
  RunLoop();
}

fuchsia::ui::views::ViewToken SemanticsIntegrationTest::CreatePresentationViewToken() {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  auto presenter = environment()->ConnectToService<fuchsia::ui::policy::Presenter>();
  presenter.set_error_handler(
      [](zx_status_t status) { FAIL() << "presenter: " << zx_status_get_string(status); });
  presenter->PresentView(std::move(view_holder_token), nullptr);

  return std::move(view_token);
}

const Node* SemanticsIntegrationTest::FindNodeWithLabel(const Node* node, zx_koid_t view_ref_koid,
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

a11y::SemanticTransform SemanticsIntegrationTest::GetTransformForNode(zx_koid_t view_ref_koid,
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

std::optional<uint32_t> SemanticsIntegrationTest::HitTest(zx_koid_t view_ref_koid,
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

fuchsia::math::PointF SemanticsIntegrationTest::CalculateCenterOfSemanticNodeBoundingBoxCoordinate(
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

bool SemanticsIntegrationTest::PerformAccessibilityAction(
    zx_koid_t view_ref_koid, uint32_t node_id, fuchsia::accessibility::semantics::Action action) {
  std::optional<bool> callback_handled;
  auto callback = [&callback_handled](bool handled) { callback_handled = handled; };
  view_manager()->PerformAccessibilityAction(view_ref_koid, node_id, action, callback);

  RunLoopUntil([&callback_handled] { return callback_handled.has_value(); });
  return *callback_handled;
}

}  // namespace accessibility_test
