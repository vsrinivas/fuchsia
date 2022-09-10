// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/semantic_tree.h"

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <cstdint>
#include <stack>
#include <string>
#include <unordered_set>

#include "src/lib/fxl/strings/concatenate.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/ui/a11y/lib/semantics/typedefs.h"

namespace a11y {
namespace {

using SemanticTreeData = std::unordered_map<uint32_t, fuchsia::accessibility::semantics::Node>;
using fuchsia::accessibility::semantics::Node;

// Tries to find |node_id| in |updated_nodes|, if not in |default_nodes|. If
// |node_id| is not present in either, returns nullptr. Please note that if
// |node_id| is present in |updated_nodes|, but the optional holds an empty
// value, this indicates a deletion and nullptr will be returned.
const Node* GetUpdatedOrDefaultNode(
    const uint32_t node_id, const std::unordered_map<uint32_t, std::optional<Node>>& updated_nodes,
    const SemanticTreeData& default_nodes) {
  if (auto it = updated_nodes.find(node_id); it != updated_nodes.end()) {
    if (it->second) {
      return &(*it->second);
    } else {
      return nullptr;
    }
  }
  if (auto it = default_nodes.find(node_id); it != default_nodes.end()) {
    return &it->second;
  }
  return nullptr;
}

// Returns a node which is a merge between |old| and |new|, where for each field
// chooses |new| if it has it, |old| otherwise.
Node MergeNodes(const Node& old_node, Node new_node) {
  Node output;
  old_node.Clone(&output);
  if (new_node.has_role()) {
    output.set_role(new_node.role());
  }

  if (new_node.has_states()) {
    output.set_states(std::move(*new_node.mutable_states()));
  }

  if (new_node.has_attributes()) {
    output.set_attributes(std::move(*new_node.mutable_attributes()));
  }

  if (new_node.has_actions()) {
    output.set_actions(new_node.actions());
  }

  if (new_node.has_child_ids()) {
    output.set_child_ids(new_node.child_ids());
  }

  if (new_node.has_location()) {
    output.set_location(new_node.location());
  }

  if (new_node.has_transform()) {
    output.set_transform(new_node.transform());
  }

  if (new_node.has_node_to_container_transform()) {
    output.set_node_to_container_transform(new_node.node_to_container_transform());
  }

  if (new_node.has_container_id()) {
    output.set_container_id(new_node.container_id());
  }

  return output;
}

// Returns true if the subtree in |nodes| resulting from an update in
// |nodes_to_be_updated|, reachable from |node_id| is acyclic and that every
// child node referenced by a parent exist. |visited_nodes| is filled with the
// node ids of this traversal.
bool ValidateSubTreeForUpdate(
    const uint32_t node_id, const uint32_t parent_id, const SemanticTreeData& nodes,
    const std::unordered_map<uint32_t, std::optional<Node>>& nodes_to_be_updated,
    std::unordered_set<uint32_t>* visited_nodes) {
  const Node* node = GetUpdatedOrDefaultNode(node_id, nodes_to_be_updated, nodes);
  if (!node) {
    // A parent node is trying to access a node that is neither in the original tree nor in the
    // updates.
    FX_LOGS(ERROR) << "Tried to visit Node [" << node_id << "] from parent [" << parent_id
                   << "], but node [" << node_id << "] does not exist or was deleted";
    return false;
  }
  if (auto it = visited_nodes->insert(node_id); !it.second) {
    // This node id has been already visited, which indicates a cycle in this tree.
    FX_LOGS(ERROR) << "Tried to visit already visited Node [" << node_id << "], possible cycle";
    return false;
  }
  if (node->has_child_ids()) {
    for (const auto& child_id : node->child_ids()) {
      if (!ValidateSubTreeForUpdate(child_id, node_id, nodes, nodes_to_be_updated, visited_nodes)) {
        return false;
      }
    }
  }
  return true;
}

// Builds a transform of the form:
// | 1  0  0  Tx |
// | 0  1  0  Ty |
// | 0  0  1  0 |
// | 0  0  0  1  |
// Where: Tx and Ty come from |offset|.
fuchsia::ui::gfx::mat4 MakeTranslationTransform(const fuchsia::ui::gfx::vec3& translation) {
  fuchsia::ui::gfx::mat4 transform;
  transform.matrix[0] = 1;
  transform.matrix[5] = 1;
  transform.matrix[10] = 1;
  transform.matrix[15] = 1;

  transform.matrix[12] = translation.x;
  transform.matrix[13] = translation.y;
  transform.matrix[14] = translation.z;
  return transform;
}

}  // namespace

SemanticTree::TreeUpdate::TreeUpdate(uint32_t delete_node_id) : delete_node_id_(delete_node_id) {}
SemanticTree::TreeUpdate::TreeUpdate(Node node) : node_(std::move(node)) {}

bool SemanticTree::TreeUpdate::has_delete_node_id() const {
  return (delete_node_id_ ? true : false);
}
bool SemanticTree::TreeUpdate::has_node() const { return (node_ ? true : false); }

uint32_t SemanticTree::TreeUpdate::TakeDeleteNodeId() {
  FX_DCHECK(has_delete_node_id());
  return std::move(*delete_node_id_);
}
Node SemanticTree::TreeUpdate::TakeNode() {
  FX_DCHECK(has_node());
  return std::move(*node_);
}

const uint32_t& SemanticTree::TreeUpdate::delete_node_id() const {
  FX_DCHECK(has_delete_node_id());
  return *delete_node_id_;
}
const Node& SemanticTree::TreeUpdate::node() const {
  FX_DCHECK(has_node());
  return *node_;
}

std::string SemanticTree::TreeUpdate::ToString() const {
  std::string output = "Update: ";

  if (has_delete_node_id()) {
    output.append("Delete Node: [" + std::to_string(delete_node_id()) + "] ");
  }
  if (has_node()) {
    std::stringstream update_node_string;

    update_node_string << "Update Node [" << std::to_string(node().node_id()) << "] Children: [";

    for (const auto child_id : node().child_ids()) {
      update_node_string << child_id << ", ";
    }
    update_node_string << ']';

    output.append(update_node_string.str());
  }
  return output;
}

SemanticTree::SemanticTree(inspect::Node inspect_node)
    : inspect_node_(std::move(inspect_node)),
      inspect_property_update_count_(inspect_node_.CreateUint(kUpdateCountInspectNodeName, 0)) {
  action_handler_ =
      [](uint32_t node_id, fuchsia::accessibility::semantics::Action action,
         fuchsia::accessibility::semantics::SemanticListener::OnAccessibilityActionRequestedCallback
             callback) {};

  hit_testing_handler_ =
      [](fuchsia::math::PointF local_point,
         fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) {};

  semantics_event_callback_ = [](SemanticsEventInfo event_info) {};

  // The first argument to |CreateLazyValues| is the name of the lazy node, and
  // will only be displayed if the callback used to generate the node's content
  // fails. Therefore, we use an error message for this node name.
  inspect_node_tree_dump_ = inspect_node_.CreateLazyValues(kTreeDumpFailedError, [this]() {
    inspect::Inspector inspector;

    FillInspectTree(inspector.GetRoot().CreateChild(kTreeDumpInspectPropertyName), &inspector);
    return fpromise::make_ok_promise(std::move(inspector));
  });
}

const Node* SemanticTree::GetNode(const uint32_t node_id) const {
  const auto it = nodes_.find(node_id);
  if (it == nodes_.end()) {
    return nullptr;
  }
  return &it->second;
}

const Node* SemanticTree::GetNextNode(const uint32_t node_id, NodeFilter filter) const {
  if (nodes_.find(node_id) == nodes_.end()) {
    return nullptr;
  }

  std::stack<uint32_t> nodes_to_visit;

  bool found_node = false;

  // Start traversal from the root node.
  nodes_to_visit.push(kRootNodeId);

  while (!nodes_to_visit.empty()) {
    auto current_node_id = nodes_to_visit.top();
    nodes_to_visit.pop();

    FX_DCHECK(nodes_.find(current_node_id) != nodes_.end())
        << "Nonexistent node id " << current_node_id << " encountered in tree traversal.";

    auto current_node = GetNode(current_node_id);

    if (found_node && filter(current_node)) {
      return current_node;
    }

    if (current_node_id == node_id) {
      found_node = true;
    }

    if (!current_node->has_child_ids() || current_node->child_ids().empty()) {
      continue;
    }

    // Add child_ids in reverse so that we visit left nodes first.
    for (auto reverse_iterator = current_node->child_ids().rbegin();
         reverse_iterator != current_node->child_ids().rend(); ++reverse_iterator) {
      const auto child_id = *reverse_iterator;
      nodes_to_visit.push(child_id);
    }
  }

  return nullptr;
}

const Node* SemanticTree::GetPreviousNode(const uint32_t node_id, NodeFilter filter) const {
  if (nodes_.find(node_id) == nodes_.end()) {
    return nullptr;
  }

  std::stack<uint32_t> nodes_to_visit;

  // Start traversal from the root node.
  nodes_to_visit.push(kRootNodeId);

  const Node* previous_returnable_node = nullptr;

  while (!nodes_to_visit.empty()) {
    auto current_node_id = nodes_to_visit.top();
    nodes_to_visit.pop();

    if (current_node_id == node_id) {
      return previous_returnable_node;
    }

    FX_DCHECK(nodes_.find(current_node_id) != nodes_.end())
        << "Nonexistent node id " << current_node_id << " encountered in tree traversal.";

    auto current_node = GetNode(current_node_id);

    if (filter(current_node)) {
      previous_returnable_node = current_node;
    }

    if (!current_node->has_child_ids() || current_node->child_ids().empty()) {
      continue;
    }

    // Add child_ids in reverse so that we visit left nodes first.
    for (auto reverse_iterator = current_node->child_ids().rbegin();
         reverse_iterator != current_node->child_ids().rend(); ++reverse_iterator) {
      const auto child_id = *reverse_iterator;
      nodes_to_visit.push(child_id);
    }
  }

  return nullptr;
}

const Node* SemanticTree::GetParentNode(const uint32_t node_id) const {
  for (const auto& kv : nodes_) {
    const auto& [unused_id, node] = kv;
    if (node.has_child_ids()) {
      const auto& child_ids = node.child_ids();
      const auto it = std::find(child_ids.begin(), child_ids.end(), node_id);
      if (it != child_ids.end()) {
        return &node;
      }
    }
  }
  return nullptr;
}

std::optional<SemanticTransform> SemanticTree::GetNodeToRootTransform(uint32_t node_id) const {
  auto* node = GetNode(node_id);

  if (!node) {
    FX_LOGS(ERROR) << "No node found iwth id: " << node_id;
    return std::nullopt;
  }

  // Compute the translation and scaling vectors for the node's bounding box.
  // Each node can supply a 4x4 transform matrix of the form:
  // [ Sx   0    0    Tx ]
  // [ 0    Sy   0    Ty ]
  // [ 0    0    Sz   Tz ]
  // [ 0    0    0    1  ]
  //
  // Here, Sx, Sy, and Sz are the scale coefficients on the x, y, and z axes,
  // respectively. Tx, Ty, and Tz are the x, y, and z components of translation,
  // respectively.
  //
  // In order to compute the transform matrix from the focused node's coordinate
  // space to the root node's coordinate space, we can simply compute the
  // cross product of the focused node's ancestors' transform matrices,
  // beginning at the focused node and up to the minimum-depth non-root ancestor
  // (the root does not have a parent, so it does not need a transform).
  //
  // [Focused node to scenic view] = [root transform] x [depth 1 ancestor transform] x
  //   [depth 2 ancestor transform] x ...  x [parent transform] x [focused node transform]
  //
  // The resulting transform will be of the same form as described above. Using
  // this matrix, we can simply extract the scaling and translation vectors
  // required by scenic: (Sx, Sy, Sz) and (Tx, Ty, Tz), respectively.
  //
  // Note that if a node has scroll offsets, it introduces a transform matrix filling only the
  // translation values to account for the scrolling. This transform is part of the computation
  // described above.

  uint32_t current_node_id = node_id;
  SemanticTransform node_to_root_transform;
  while (true) {
    auto current_node = GetNode(current_node_id);
    FX_DCHECK(current_node);

    // Don't apply scrolling that's on the target node, since scrolling affects
    // the location of its children rather than it.  Apply scrolling before the
    // node's transform, since the scrolling moves its children within it and
    // then the transform moves the result to the parent's space.
    if (current_node_id != node_id && current_node->has_states() &&
        current_node->states().has_viewport_offset()) {
      auto translation_matrix =
          MakeTranslationTransform({-current_node->states().viewport_offset().x,
                                    -current_node->states().viewport_offset().y});
      node_to_root_transform.ChainLocalTransform(translation_matrix);
    }

    if (current_node->has_node_to_container_transform()) {
      // Apply explicit transform.
      node_to_root_transform.ChainLocalTransform(current_node->node_to_container_transform());
    } else if (current_node->has_transform()) {
      node_to_root_transform.ChainLocalTransform(current_node->transform());
    }

    // Once we have applied the root node's transform, we should exit the loop.
    if (current_node_id == 0) {
      break;
    }

    // If |current_node| has an offset container specified, then its transform
    // puts local coordinates into the coordinate space of the offset container
    // node, NOT the parent of |current_node|. If no offset container is
    // specified, then we assume the transform is relative to the parent.
    if (current_node->has_container_id()) {
      const auto container_id = current_node->container_id();

      // It's possible for a node to have a container id equal to its own id.
      // In this case, this node's coordinate space will be equivalent to
      // "root" space, so we should terminate the loop here.
      if (container_id == current_node_id) {
        break;
      }

      // The `node_to_container_transform` does NOT account for the implied
      // translation with respect to the offset container's bounds, so we must
      // apply that translation explicitly here.
      //
      // NOTE: We do NOT want to apply this translation if:
      //   (1) This node is the root node, OR
      //   (2) This node is its own offset container.
      //
      // We check that the `transform` (deprecated) field is NOT set, as
      // opposed to checking that `node_to_container_transform` IS set, in
      // order to support the transition from `transform` to
      // `node_to_container_transform`. Once the transition is complete,
      // we can remove this condition. There are four cases we need to
      // accommodate:
      //
      //   (1) The client node has an explicit transform AND uses the
      //   `transform` field. In this case, we should not apply the implied
      //   translation here.
      //   (2) The client node does NOT have an explicit transform AND uses the
      //   `transform` field.
      //   (3) The client node has an explicit transform AND uses the
      //   `node_to_container_transform` field.
      //   (4) The client node does NOT have an explicit transform AND uses the
      //   `node_to_container_transform` field.
      //
      //   We should only apply the implicit offset in cases (3) and (4). In
      //   in case 4, `node_to_container_transform` will NOT be set, so we can't
      //   simply check that this field is set. Rather, since `transform` will
      //   be unset in both cases (3) and (4), we can use the !has_transform()
      //   condition. Notice that `transform` will always be set in case (1).
      //   It's possible that `transform` is unset in case (2). however, since
      //   the transform only accounts for the translation with respect to the
      //   offset container's bounds in case (2), the only way this field could
      //   be unset is if the offset container's bounds.min is at (0, 0), in
      //   which case applying the implied translation is a NOOP.
      //
      // TODO(fxb.dev/87181): Remove uses of `transform` field.
      if (!current_node->has_transform()) {
        auto container_node = GetNode(container_id);
        FX_DCHECK(container_node);

        auto translation_matrix = MakeTranslationTransform(container_node->location().min);
        node_to_root_transform.ChainLocalTransform(translation_matrix);
      }

      current_node_id = container_id;
    } else {
      auto parent_node = GetParentNode(current_node_id);
      FX_DCHECK(parent_node);
      current_node_id = parent_node->node_id();
    }
  }

  return node_to_root_transform;
}

bool SemanticTree::Update(TreeUpdates updates) {
  nodes_to_be_updated_.clear();  // Prepares for a new update.
  if (updates.empty()) {
    return true;
  }
  for (auto& update : updates) {
    inspect_property_update_count_.Set(++update_count_);
    if (update.has_delete_node_id()) {
      auto delete_node_id = update.TakeDeleteNodeId();
      // If the node we're deleting doesn't exist in the pre-commit tree, then
      // the deletion is effectively a no-op (even if the deletion is preceded by
      // updates to the same node), as the post-commit tree state also won't
      // contain this node. Note that since we support partial tree updates,
      // this logic ONLY applies in the case that a node does not exist
      // pre-commit.
      auto nodes_it = nodes_.find(delete_node_id);
      auto nodes_to_be_updated_it = nodes_to_be_updated_.find(delete_node_id);
      auto nodes_to_be_updated_found = nodes_to_be_updated_it != nodes_to_be_updated_.end();
      if (nodes_it == nodes_.end() && nodes_to_be_updated_found) {
        nodes_to_be_updated_.erase(nodes_to_be_updated_it);
      } else if (nodes_to_be_updated_found) {
        nodes_to_be_updated_it->second.reset();
      } else {
        nodes_to_be_updated_[delete_node_id].reset();
      }
    } else if (update.has_node()) {
      MarkNodeForUpdate(update.TakeNode());
    }
  }

  std::unordered_set<uint32_t> visited_nodes;
  if (!ValidateUpdate(&visited_nodes)) {
    return false;
  }
  ApplyNodeUpdates(visited_nodes);

  SemanticsEventInfo event_info = {.event_type = SemanticsEventType::kSemanticTreeUpdated};
  OnSemanticsEvent(std::move(event_info));
  return true;
}

bool SemanticTree::ValidateUpdate(std::unordered_set<uint32_t>* visited_nodes) {
  const Node* root = GetUpdatedOrDefaultNode(kRootNodeId, nodes_to_be_updated_, nodes_);
  if (!root) {
    // Note that there are only three occasions where the root could be null:
    // 1. The tree is empty and this is a new update to the tree without a root
    // (invalid).
    // 2. This is an update that explicitly deletes the root node (valid). This
    // effectively causes the tree to be garbage collected and all nodes are
    // deleted.
    // 3. This update creates, and then deletes the same set of nodes, starting
    // from an empty tree (valid). In this case, the post-commit tree is also empty,
    // which remains a valid state.
    if (auto it = nodes_to_be_updated_.find(kRootNodeId); it != nodes_to_be_updated_.end()) {
      return true;
    } else {
      // If nodes_to_be_updated_ is empty, then the pre- and post-commit states
      // must be identical. The pre-commit state must have been valid, so the
      // post-commit state must also be valid.
      return nodes_to_be_updated_.empty();
    }
  }
  if (!ValidateSubTreeForUpdate(kRootNodeId, 0 /* parent id, only used to print error message */,
                                nodes_, nodes_to_be_updated_, visited_nodes)) {
    return false;
  }
  return true;
}

void SemanticTree::MarkNodeForUpdate(Node node) {
  const uint32_t node_id = node.node_id();
  if (const Node* old = GetUpdatedOrDefaultNode(node_id, nodes_to_be_updated_, nodes_);
      old == nullptr) {
    // New node. Simply mark it for future update.
    nodes_to_be_updated_[node_id] = std::move(node);
  } else {
    // Partial update.
    nodes_to_be_updated_[node_id] = MergeNodes(*old, std::move(node));
  }
}

void SemanticTree::ApplyNodeUpdates(const std::unordered_set<uint32_t>& visited_nodes) {
  // First, apply all pending updates and then delete dangling subtrees.
  for (auto& kv : nodes_to_be_updated_) {
    auto& [node_id, updated_node] = kv;
    if (updated_node) {
      nodes_[node_id] = std::move(*updated_node);
    } else {
      // The optional holds an empty value, indicating a deletion.
      nodes_.erase(node_id);
    }
  }

  // Delete dangling subtrees.
  auto it = nodes_.begin();
  while (it != nodes_.end()) {
    if (auto visited_it = visited_nodes.find(it->first); visited_it == visited_nodes.end()) {
      // node unreachable. remove from the tree.
      it = nodes_.erase(it);
    } else {
      ++it;
    }
  }
}

void SemanticTree::Clear() {
  nodes_.clear();
  SemanticsEventInfo event_info = {.event_type = SemanticsEventType::kSemanticTreeUpdated};
  OnSemanticsEvent(std::move(event_info));
}

void SemanticTree::PerformAccessibilityAction(
    uint32_t node_id, fuchsia::accessibility::semantics::Action action,
    fuchsia::accessibility::semantics::SemanticListener::OnAccessibilityActionRequestedCallback
        callback) const {
  action_handler_(node_id, action, std::move(callback));
}

void SemanticTree::PerformHitTesting(
    fuchsia::math::PointF local_point,
    fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) const {
  hit_testing_handler_(local_point, std::move(callback));
}

std::string vec2ToString(const fuchsia::ui::gfx::vec2 vec) {
  return fxl::StringPrintf("(x: %.1f, y: %.1f)", vec.x, vec.y);
}

std::string vec3ToString(const fuchsia::ui::gfx::vec3 vec) {
  return fxl::StringPrintf("(x: %.1f, y: %.1f, z: %.1f)", vec.x, vec.y, vec.z);
}

std::string mat4ToString(const fuchsia::ui::gfx::mat4 mat) {
  std::string retval = "{ ";
  for (int i = 0; i < 4; i++) {
    retval.append(fxl::StringPrintf("col%d: (%.1f,%.1f,%.1f,%.1f), ", i, mat.matrix[i * 4],
                                    mat.matrix[i * 4 + 1], mat.matrix[i * 4 + 2],
                                    mat.matrix[i * 4 + 3]));
  }
  return retval.append(" }");
}

std::string locationToString(const fuchsia::ui::gfx::BoundingBox location) {
  return fxl::Concatenate(
      {"{ min: ", vec3ToString(location.min), ", max: ", vec3ToString(location.max), " }"});
}

std::string roleToString(const fuchsia::accessibility::semantics::Role role) {
  switch (role) {
    case fuchsia::accessibility::semantics::Role::UNKNOWN:
      return "UNKNOWN";
    case fuchsia::accessibility::semantics::Role::BUTTON:
      return "BUTTON";
    case fuchsia::accessibility::semantics::Role::HEADER:
      return "HEADER";
    case fuchsia::accessibility::semantics::Role::IMAGE:
      return "IMAGE";
    case fuchsia::accessibility::semantics::Role::TEXT_FIELD:
      return "TEXT_FIELD";
    case fuchsia::accessibility::semantics::Role::SLIDER:
      return "SLIDER";
    case fuchsia::accessibility::semantics::Role::LINK:
      return "LINK";
    case fuchsia::accessibility::semantics::Role::CHECK_BOX:
      return "CHECK_BOX";
    case fuchsia::accessibility::semantics::Role::RADIO_BUTTON:
      return "RADIO_BUTTON";
    case fuchsia::accessibility::semantics::Role::LIST:
      return "LIST";
    case fuchsia::accessibility::semantics::Role::LIST_ELEMENT_MARKER:
      return "LIST_ELEMENT_MARKER";
    case fuchsia::accessibility::semantics::Role::STATIC_TEXT:
      return "STATIC_TEXT";
    case fuchsia::accessibility::semantics::Role::TOGGLE_SWITCH:
      return "TOGGLE_SWITCH";
    default:
      return "Default";
  }
}

std::string actionToString(const fuchsia::accessibility::semantics::Action action) {
  switch (action) {
    case fuchsia::accessibility::semantics::Action::DEFAULT:
      return "DEFAULT";
    case fuchsia::accessibility::semantics::Action::SECONDARY:
      return "SECONDARY";
    case fuchsia::accessibility::semantics::Action::SET_FOCUS:
      return "SET_FOCUS";
    case fuchsia::accessibility::semantics::Action::SET_VALUE:
      return "SET_VALUE";
    case fuchsia::accessibility::semantics::Action::SHOW_ON_SCREEN:
      return "SHOW_ON_SCREEN";
    case fuchsia::accessibility::semantics::Action::DECREMENT:
      return "DECREMENT";
    case fuchsia::accessibility::semantics::Action::INCREMENT:
      return "INCREMENT";
    default:
      return "No Action Found";
  }
}

std::string actionsToString(const std::vector<fuchsia::accessibility::semantics::Action>& actions) {
  std::string retval = "{ ";
  for (const auto& action : actions) {
    retval.append(fxl::StringPrintf("%s, ", actionToString(action).c_str()));
  }
  return retval.append("}");
}

std::string checkedStateToString(
    const fuchsia::accessibility::semantics::CheckedState& checked_state) {
  switch (checked_state) {
    case fuchsia::accessibility::semantics::CheckedState::NONE:
      return "NONE";
    case fuchsia::accessibility::semantics::CheckedState::CHECKED:
      return "CHECKED";
    case fuchsia::accessibility::semantics::CheckedState::UNCHECKED:
      return "UNCHECKED";
    case fuchsia::accessibility::semantics::CheckedState::MIXED:
      return "MIXED";
    default:
      return "No checked state found";
  }
}

std::string toggledStateToString(
    const fuchsia::accessibility::semantics::ToggledState& toggled_state) {
  switch (toggled_state) {
    case fuchsia::accessibility::semantics::ToggledState::ON:
      return "ON";
    case fuchsia::accessibility::semantics::ToggledState::OFF:
      return "OFF";
    case fuchsia::accessibility::semantics::ToggledState::INDETERMINATE:
      return "INDETERMINATE";
    default:
      return "No toggled state found";
  }
}

void SemanticTree::FillInspectTree(inspect::Node inspect_node,
                                   inspect::Inspector* inspector) const {
  std::function<void(const Node*, int, inspect::Node)> fillTree;

  fillTree = [this, inspector, &fillTree](const Node* node, int current_level,
                                          inspect::Node inspect_node) {
    if (!node) {
      return;
    }

    inspect_node.CreateUint("id", node->node_id(), inspector);
    if (node->has_attributes() && node->attributes().has_label()) {
      inspect_node.CreateString("label", node->attributes().label(), inspector);
    }
    if (node->has_location()) {
      inspect_node.CreateString("location", locationToString(node->location()), inspector);
    }
    if (node->has_container_id()) {
      inspect_node.CreateUint("offset_container_id", node->container_id(), inspector);
    }
    if (node->has_transform()) {
      inspect_node.CreateString("transform", mat4ToString(node->transform()), inspector);
    }
    if (node->has_node_to_container_transform()) {
      inspect_node.CreateString("node_to_container_transform",
                                mat4ToString(node->node_to_container_transform()), inspector);
    }
    if (node->has_role()) {
      inspect_node.CreateString("role", roleToString(node->role()), inspector);
    }
    if (node->has_actions()) {
      inspect_node.CreateString("action", actionsToString(node->actions()), inspector);
    }
    if (node->has_states()) {
      const auto& node_states = node->states();
      if (node_states.has_checked_state()) {
        inspect_node.CreateString("checked_state",
                                  checkedStateToString(node_states.checked_state()), inspector);
      }

      if (node_states.has_selected()) {
        inspect_node.CreateBool("selected", node_states.selected(), inspector);
      }

      if (node_states.has_hidden()) {
        inspect_node.CreateBool("hidden", node_states.hidden(), inspector);
      }

      if (node_states.has_value()) {
        inspect_node.CreateString("value", node_states.value(), inspector);
      }

      if (node_states.has_range_value()) {
        inspect_node.CreateDouble("range_value", node_states.range_value(), inspector);
      }

      if (node_states.has_viewport_offset()) {
        inspect_node.CreateString("viewport_offset", vec2ToString(node_states.viewport_offset()),
                                  inspector);
      }

      if (node_states.has_toggled_state()) {
        inspect_node.CreateString("toggled_state",
                                  toggledStateToString(node_states.toggled_state()), inspector);
      }

      if (node_states.has_focusable()) {
        inspect_node.CreateBool("focusable", node_states.focusable(), inspector);
      }

      if (node_states.has_has_input_focus()) {
        inspect_node.CreateBool("has_input_focus", node_states.has_input_focus(), inspector);
      }
    }
    if (node->has_attributes()) {
      const auto& node_attributes = node->attributes();

      if (node_attributes.has_secondary_label()) {
        inspect_node.CreateString("secondary_label", node_attributes.secondary_label(), inspector);
      }

      if (node_attributes.has_secondary_action_description()) {
        inspect_node.CreateString("secondary_action_description",
                                  node_attributes.secondary_action_description(), inspector);
      }

      if (node_attributes.has_range()) {
        const auto& range_attributes = node_attributes.range();

        if (range_attributes.has_min_value()) {
          inspect_node.CreateDouble("min_value", range_attributes.min_value(), inspector);
        }

        if (range_attributes.has_max_value()) {
          inspect_node.CreateDouble("max_vlaue", range_attributes.max_value(), inspector);
        }

        if (range_attributes.has_step_delta()) {
          inspect_node.CreateDouble("step_delta", range_attributes.step_delta(), inspector);
        }
      }

      if (node_attributes.has_hierarchical_level()) {
        inspect_node.CreateUint("hierarchical_level", node_attributes.hierarchical_level(),
                                inspector);
      }

      if (node_attributes.has_is_keyboard_key()) {
        inspect_node.CreateBool("is_keyboard_key", node_attributes.is_keyboard_key(), inspector);
      }
    }

    if (!node->has_child_ids()) {
      inspector->emplace(std::move(inspect_node));
      return;
    }

    int width = static_cast<int>(std::to_string(node->child_ids().size() - 1).length());
    int child_index = 0;
    for (const auto& child_id : node->child_ids()) {
      const auto* child = GetNode(child_id);
      FX_DCHECK(child);

      auto name = fxl::StringPrintf("node_idx_%0*d_id_%u", width, child_index++, child->node_id());
      fillTree(child, current_level + 1, inspect_node.CreateChild(name));
    }
    inspector->emplace(std::move(inspect_node));
  };

  const auto* root = GetNode(kRootNodeId);

  if (!root) {
    inspect_node.CreateString(kTreeDumpInspectPropertyName, "Root Node not found.", inspector);
    inspector->emplace(std::move(inspect_node));
    return;
  }

  fillTree(root, 0, std::move(inspect_node));
}

std::string SemanticTree::ToString() const {
  std::function<void(const Node*, int, std::string*)> printNode;

  printNode = [&printNode, this](const Node* node, int current_level, std::string* output) {
    if (!node) {
      return;
    }

    // Add indentation
    output->append(4 * current_level, ' ');

    *output = fxl::Concatenate(
        {*output, "ID: ", std::to_string(node->node_id()), " Label:",
         node->has_attributes() && node->attributes().has_label() ? node->attributes().label()
                                                                  : "no label",
         " Location: ", node->has_location() ? locationToString(node->location()) : "no location",
         " Transform: ", node->has_transform() ? mat4ToString(node->transform()) : "no transform",
         " Role: ", node->has_role() ? roleToString(node->role()) : "no role",
         " Action: ", node->has_actions() ? actionsToString(node->actions()) : "no actions", "\n"});

    if (!node->has_child_ids()) {
      return;
    }
    for (const auto& child_id : node->child_ids()) {
      const auto* child = GetNode(child_id);
      FX_DCHECK(child);
      printNode(child, current_level + 1, output);
    }
  };

  const auto* root = GetNode(kRootNodeId);
  std::string tree_string;

  if (!root) {
    tree_string = "Root Node not found.";
    return tree_string;
  }

  printNode(root, kRootNodeId, &tree_string);

  return tree_string;
}

// TODO(fxbug.dev/55220): Refine definition of describability.
bool SemanticTree::NodeIsDescribable(const fuchsia::accessibility::semantics::Node* node) const {
  return node &&
         ((node->has_attributes() && node->attributes().has_label() &&
           !node->attributes().label().empty()) ||
          (node->has_role() && node->role() == fuchsia::accessibility::semantics::Role::BUTTON));
}

void SemanticTree::OnSemanticsEvent(SemanticsEventInfo event_info) {
  semantics_event_callback_(std::move(event_info));
}

}  // namespace a11y
