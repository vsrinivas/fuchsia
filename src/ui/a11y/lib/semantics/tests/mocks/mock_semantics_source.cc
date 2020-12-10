// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantics_source.h"

#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {

void MockSemanticsSource::AddViewRef(fuchsia::ui::views::ViewRef view_ref) {
  view_ref_ = std::move(view_ref);
}

bool MockSemanticsSource::ViewHasSemantics(zx_koid_t view_ref_koid) {
  return view_ref_koid == a11y::GetKoid(view_ref_);
}

std::optional<fuchsia::ui::views::ViewRef> MockSemanticsSource::ViewRefClone(
    zx_koid_t view_ref_koid) {
  if (!ViewHasSemantics(view_ref_koid)) {
    return std::nullopt;
  }
  return a11y::Clone(view_ref_);
}

void MockSemanticsSource::CreateSemanticNode(zx_koid_t koid,
                                             fuchsia::accessibility::semantics::Node node) {
  nodes_[koid][node.node_id()] = std::move(node);
}

const fuchsia::accessibility::semantics::Node* MockSemanticsSource::GetSemanticNode(
    zx_koid_t koid, uint32_t node_id) const {
  const auto it = nodes_.find(koid);
  if (it == nodes_.end()) {
    return nullptr;
  }
  const auto& nodes_for_view = it->second;
  const auto node_it = nodes_for_view.find(node_id);
  if (node_it == nodes_for_view.end()) {
    return nullptr;
  }
  return &node_it->second;
}

const fuchsia::accessibility::semantics::Node* MockSemanticsSource::GetParentNode(
    zx_koid_t koid, uint32_t node_id) const {
  const auto it = nodes_.find(koid);
  if (it == nodes_.end()) {
    return nullptr;
  }

  const auto& nodes_for_view = it->second;

  for (const auto& node : nodes_for_view) {
    if (!node.second.has_child_ids()) {
      continue;
    }

    for (const auto child_id : node.second.child_ids()) {
      if (child_id == node_id) {
        return &node.second;
      }
    }
  }

  return nullptr;
}

const fuchsia::accessibility::semantics::Node* MockSemanticsSource::GetNextNode(
    zx_koid_t koid, uint32_t node_id,
    fit::function<bool(const fuchsia::accessibility::semantics::Node*)> filter) const {
  if (nodes_.find(koid) == nodes_.end()) {
    return nullptr;
  }

  const auto& nodes = nodes_.at(koid);

  std::map<uint32_t, fuchsia::accessibility::semantics::Node>::const_iterator it =
      nodes.find(node_id);

  if (it == nodes.end()) {
    return nullptr;
  }

  if (++it != nodes.end()) {
    return &(it->second);
  }

  return nullptr;
}

const fuchsia::accessibility::semantics::Node* MockSemanticsSource::GetPreviousNode(
    zx_koid_t koid, uint32_t node_id,
    fit::function<bool(const fuchsia::accessibility::semantics::Node*)> filter) const {
  if (nodes_.find(koid) == nodes_.end()) {
    return nullptr;
  }

  const auto& nodes = nodes_.at(koid);

  std::map<uint32_t, fuchsia::accessibility::semantics::Node>::const_iterator it =
      nodes.find(node_id);

  if (it == nodes.end()) {
    return nullptr;
  }

  if (it != nodes.begin()) {
    return &(std::prev(it)->second);
  }

  return nullptr;
}

void MockSemanticsSource::SetHitTestResult(zx_koid_t koid,
                                           fuchsia::accessibility::semantics::Hit hit_test_result) {
  hit_test_results_[koid] = std::move(hit_test_result);
}

void MockSemanticsSource::ExecuteHitTesting(
    zx_koid_t koid, fuchsia::math::PointF local_point,
    fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) {
  // NOTE: We don't need to check if we have specified a hit test result for the given koid. If we
  // haven't, operator[] will default construct an empty hit test result, which is the desired
  // behavior in this case.
  callback(std::move(hit_test_results_[koid]));
  hit_test_results_.erase(koid);
}

void MockSemanticsSource::SetActionResult(zx_koid_t koid, bool action_result) {
  action_results_[koid] = action_result;
}

void MockSemanticsSource::PerformAccessibilityAction(
    zx_koid_t koid, uint32_t node_id, fuchsia::accessibility::semantics::Action action,
    fuchsia::accessibility::semantics::SemanticListener::OnAccessibilityActionRequestedCallback
        callback) {
  requested_actions_[koid].emplace_back(node_id, action);
  callback(true);
}

const std::vector<std::pair<uint32_t, fuchsia::accessibility::semantics::Action>>&
MockSemanticsSource::GetRequestedActionsForView(zx_koid_t koid) {
  return requested_actions_[koid];
}

}  // namespace accessibility_test
