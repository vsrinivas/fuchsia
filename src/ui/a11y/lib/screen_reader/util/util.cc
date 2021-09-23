// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/util/util.h"

#include <lib/syslog/cpp/macros.h>

#include <string>

#include "fuchsia/accessibility/semantics/cpp/fidl.h"

namespace a11y {

namespace {

bool NodeHasSubsetOfActions(const fuchsia::accessibility::semantics::Node* node_1,
                            const fuchsia::accessibility::semantics::Node* node_2) {
  // If node_1 doesn't have any actions, then by definition, its actions must be
  // a subset of node_2's actions.
  if (!node_1->has_actions() || node_1->actions().empty()) {
    return true;
  }

  // If node_1 does have actions, and node_2 doesn't, then node_1's actions are
  // not a subset of node_2's.
  if (!node_2->has_actions() || node_2->actions().empty()) {
    return false;
  }

  const auto& node_1_actions = node_1->actions();
  const auto& node_2_actions = node_2->actions();

  // If node_1 contains more actions than node_2, then its actions cannot be a
  // subset of node_2's.
  if (node_1_actions.size() > node_2_actions.size()) {
    return false;
  }

  std::set<fuchsia::accessibility::semantics::Action> node_2_actions_set(node_2_actions.begin(),
                                                                         node_2_actions.end());

  for (const auto node_1_action : node_1_actions) {
    // If node_1 has an action that node_2 does not, then node_1's actions are
    // not a subset of node_2's.
    if (node_2_actions_set.find(node_1_action) == node_2_actions_set.end()) {
      return false;
    }
  }

  return true;
}

}  // namespace

bool NodeIsDescribable(const fuchsia::accessibility::semantics::Node* node) {
  if (!node) {
    return false;
  }
  if (node->has_states() && node->states().has_hidden() && node->states().hidden()) {
    return false;
  }

  bool contains_text = node->has_attributes() && node->attributes().has_label() &&
                       !node->attributes().label().empty();
  bool is_actionable =
      node->has_role() && (node->role() == fuchsia::accessibility::semantics::Role::BUTTON ||
                           node->role() == fuchsia::accessibility::semantics::Role::TOGGLE_SWITCH ||
                           node->role() == fuchsia::accessibility::semantics::Role::RADIO_BUTTON ||
                           node->role() == fuchsia::accessibility::semantics::Role::LINK ||
                           node->role() == fuchsia::accessibility::semantics::Role::CHECK_BOX ||
                           node->role() == fuchsia::accessibility::semantics::Role::SLIDER ||
                           node->role() == fuchsia::accessibility::semantics::Role::IMAGE);
  return contains_text || is_actionable;
}

std::string FormatFloat(float input) {
  auto output = std::to_string(input);
  FX_DCHECK(!output.empty());

  // If the output string does not contain a decimal point, we don't need to
  // trim trailing zeros.
  auto location_of_decimal = output.find('.');
  if (location_of_decimal == std::string::npos) {
    return output;
  }

  auto location_of_last_non_zero_character = output.find_last_not_of('0');
  // If the last non-zero character is a decimal point, the value is an integer,
  // so we should remove the decimal point and trailing zeros.
  if (location_of_last_non_zero_character == location_of_decimal) {
    return output.erase(location_of_decimal, std::string::npos);
  }

  // If the last digit is non-zero, the string has no trailing zeros, so return
  // the string as is.
  if (location_of_last_non_zero_character == output.size() - 1) {
    return output;
  }

  // In the last remaining case, the string represents a decimal with trailing
  // zeros.
  return output.erase(location_of_last_non_zero_character + 1, std::string::npos);
}

std::set<uint32_t> GetNodesToExclude(zx_koid_t koid, uint32_t node_id,
                                     SemanticsSource* semantics_source) {
  std::set<uint32_t> nodes_to_exclude;

  auto node = semantics_source->GetSemanticNode(koid, node_id);
  if (!node) {
    return nodes_to_exclude;
  }

  // We are only interested in the motif where one node has several descendants
  // with the same label.
  if (!node->has_attributes() || !node->attributes().has_label()) {
    return nodes_to_exclude;
  }

  const auto label = node->attributes().label();

  if (node->has_child_ids()) {
    auto current_node = node;
    while (current_node) {
      // If current node does not have the same label as node, then the one
      // label linear subtree motif is not present.
      if (current_node->has_attributes() && current_node->attributes().has_label() &&
          current_node->attributes().label() != label) {
        nodes_to_exclude.clear();
        break;
      }

      // If we have reached a leaf, then the linear motif is present.
      if (!current_node->has_child_ids() || current_node->child_ids().empty()) {
        break;
      }

      // If any node in the subtree has multiple children, then the linear motif
      // is not present.
      if (current_node->child_ids().size() != 1) {
        nodes_to_exclude.clear();
        break;
      }

      // Visit the only child of the current node next.
      current_node = semantics_source->GetSemanticNode(koid, current_node->child_ids().at(0));
      if (!current_node) {
        // Node was deleted, no need to continue;
        nodes_to_exclude.clear();
        return nodes_to_exclude;
      }

      // If the current node has any actions that the node in question does not,
      // then we should not add the current node to the set of nodes to skip.
      if (NodeHasSubsetOfActions(current_node, node)) {
        nodes_to_exclude.emplace(current_node->node_id());
      }
    }
  }

  if (!nodes_to_exclude.empty() || !node->has_child_ids() || node->child_ids().empty()) {
    auto current_node = semantics_source->GetParentNode(koid, node_id);
    if (!current_node) {
      // Node was deleted, no need to continue;
      nodes_to_exclude.clear();
      return nodes_to_exclude;
    }

    while (current_node) {
      if (!current_node->has_attributes() || !current_node->attributes().has_label()) {
        break;
      }

      // If current node does not have the same label as node, then the one
      // label linear subtree motif is not present.
      if (current_node->attributes().label() != label) {
        break;
      }

      if (current_node->child_ids().size() != 1) {
        break;
      }

      // If the current node has any actions that the node in question does not,
      // then we should not add the current node to the set of nodes to skip.
      if (NodeHasSubsetOfActions(current_node, node)) {
        nodes_to_exclude.emplace(current_node->node_id());
      }

      current_node = semantics_source->GetParentNode(koid, current_node->node_id());
    }
  }

  return nodes_to_exclude;
}

bool NodeIsSlider(const fuchsia::accessibility::semantics::Node& node) {
  bool has_role_slider =
      node.has_role() && node.role() == fuchsia::accessibility::semantics::Role::SLIDER;
  bool has_range_value = node.has_states() && node.states().has_range_value();

  return has_role_slider || has_range_value;
}

std::string GetSliderValue(const fuchsia::accessibility::semantics::Node& node) {
  if (!node.has_states()) {
    return std::string();
  }

  if (node.states().has_range_value()) {
    return std::to_string(static_cast<int>((node.states().range_value())));
  }

  if (node.states().has_value() && !node.states().value().empty()) {
    return node.states().value();
  }

  return std::string();
}

}  // namespace a11y
