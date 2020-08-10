// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/tests/semantics_integration_test_fixture.h"

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
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
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_semantics.h"

namespace accessibility_test {

namespace {

constexpr zx::duration kTimeout = zx::sec(60);

}  // namespace

SemanticsIntegrationTest::SemanticsIntegrationTest(const std::string& environment_label)
    : environment_label_(environment_label),
      component_context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
      view_manager_(std::make_unique<a11y::SemanticTreeServiceFactory>(),
                    std::make_unique<MockViewSemanticsFactory>(),
                    std::make_unique<MockAnnotationViewFactory>(), component_context_.get(),
                    component_context_->outgoing()->debug_dir()),
      scenic_(component_context_->svc()->Connect<fuchsia::ui::scenic::Scenic>()) {
  scenic_.set_error_handler([](zx_status_t status) {
    FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
  });

  // Wait for scenic to get initialized by calling GetDisplayInfo.
  scenic_->GetDisplayInfo([this](fuchsia::ui::gfx::DisplayInfo info) { QuitLoop(); });
  RunLoopWithTimeout(kTimeout);
}

void SemanticsIntegrationTest::SetUp() {
  TestWithEnvironment::SetUp();
  // This is done in |SetUp| as opposed to the constructor to allow subclasses the opportunity to
  // override |CreateServices()|.
  auto services = TestWithEnvironment::CreateServices();
  services->AddService(semantics_manager_bindings_.GetHandler(&view_manager_));

  CreateServices(services);

  environment_ = CreateNewEnclosingEnvironment(environment_label_, std::move(services),
                                               {.inherit_parent_services = true});
}

fuchsia::ui::views::ViewToken SemanticsIntegrationTest::CreatePresentationViewToken() {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  auto presenter = real_services()->Connect<fuchsia::ui::policy::Presenter>();
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
  std::stack<const Node*> path;
  // Perform a DFS to find the path to the target node
  std::function<bool(const Node*)> traverse = [&](const Node* node) {
    if (node->node_id() == node_id) {
      path.push(node);
      return true;
    }
    if (!node->has_child_ids()) {
      return false;
    }
    for (const auto& child_id : node->child_ids()) {
      const auto* child = view_manager()->GetSemanticNode(view_ref_koid, child_id);
      FX_DCHECK(child);
      if (traverse(child)) {
        path.push(node);
        return true;
      }
    }
    return false;
  };

  auto root = view_manager()->GetSemanticNode(view_ref_koid, 0u);
  traverse(root);

  a11y::SemanticTransform transform;
  while (!path.empty()) {
    auto node = path.top();
    if (node->has_transform()) {
      transform.ChainLocalTransform(node->transform());
    }
    path.pop();
  }

  return transform;
}

std::optional<uint32_t> SemanticsIntegrationTest::HitTest(zx_koid_t view_ref_koid,
                                                          fuchsia::math::PointF target) {
  std::optional<fuchsia::accessibility::semantics::Hit> target_hit;
  auto hit_callback = [&](fuchsia::accessibility::semantics::Hit hit) {
    target_hit = std::move(hit);
  };

  view_manager()->ExecuteHitTesting(view_ref_koid, target, hit_callback);

  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&] { return target_hit.has_value(); }, kTimeout));
  if (!target_hit.has_value() || !target_hit->has_node_id()) {
    return std::nullopt;
  }
  return target_hit->node_id();
}

fuchsia::math::PointF SemanticsIntegrationTest::CalculateViewTargetPoint(
    zx_koid_t view_ref_koid, const fuchsia::accessibility::semantics::Node* node,
    fuchsia::math::PointF offset) {
  // Semantic trees may have transforms in each node.  That transform defines the spatial relation
  // between coordinates in the node's space to coordinates in it's parent's space.  This is done
  // to enable semantic providers to avoid recomputing location information on every child node
  // when a parent node (or the entire view) undergoes a spatial change.

  // Get the transform from the node's local space to the view's local space.
  auto transform = GetTransformForNode(view_ref_koid, node->node_id());
  // Calculate the point within the node's local space we want to target
  fuchsia::ui::gfx::vec3 node_local_target_point = {
      node->location().min.x + offset.x,
      node->location().min.y + offset.y,
      node->location().min.z,
  };
  // Transform that point into the view's local space.
  auto view_local_target_point = transform.Apply(node_local_target_point);
  return {view_local_target_point.x, view_local_target_point.y};
}

}  // namespace accessibility_test
