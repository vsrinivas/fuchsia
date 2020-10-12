// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_SEMANTIC_TREE_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_SEMANTIC_TREE_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/math/cpp/fidl.h>
#include <lib/inspect/cpp/inspect.h>

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/ui/a11y/lib/semantics/semantics_event.h"

namespace a11y {

// A Semantic Tree represents the relationship of elements on an UI in the form of Semantic Nodes,
// which are provided by runtimes and normally consumed by assistive technology.
//
// This tree is always in a valid state, and accepts changes via calls to
// Update(). An update can be applied to the tree if it leaves the tree in a
// valid state, or rejected, if it leads to an invalid one.
//
// It is also true for a tree to be considered valid:
// 1. It can be either empty or starts with a root which id is equal to kRootNodeId.
// 2. The tree is acyclic.
// 3. Every child id pointed by its parent must exist in the tree.
class SemanticTree {
 public:
  static constexpr uint32_t kRootNodeId = 0;

  // Represents a single tree update. It can be a deletion or a node.
  class TreeUpdate {
   public:
    TreeUpdate(uint32_t delete_node_id);
    TreeUpdate(fuchsia::accessibility::semantics::Node node);
    bool has_delete_node_id() const;
    bool has_node() const;
    const uint32_t& delete_node_id() const;
    uint32_t TakeDeleteNodeId();
    const fuchsia::accessibility::semantics::Node& node() const;
    fuchsia::accessibility::semantics::Node TakeNode();
    std::string ToString() const;

   private:
    // If present, deletes the node with |node_id|.
    std::optional<uint32_t> delete_node_id_;
    // If present, uses node to update in the tree.
    std::optional<fuchsia::accessibility::semantics::Node> node_;
  };

  using TreeUpdates = std::vector<TreeUpdate>;

  using ActionHandlerCallback = fit::function<void(
      uint32_t node_id, fuchsia::accessibility::semantics::Action action,
      fuchsia::accessibility::semantics::SemanticListener::OnAccessibilityActionRequestedCallback
          callback)>;

  using HitTestingHandlerCallback = fit::function<void(
      fuchsia::math::PointF local_point,
      fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback)>;

  using SemanticsEventCallback = fit::function<void(SemanticsEventType event_type)>;

  static constexpr char kUpdateCountInspectNodeName[] = "tree_update_count";
  static constexpr char kTreeDumpInspectPropertyName[] = "semantic_tree";
  static constexpr char kTreeDumpFailedError[] = "tree_dump_failed";

  // A SemanticTree object is normally maintained by a semantics provider while
  // being consumed by a semantics consumer (such as a screen reader).
  explicit SemanticTree(inspect::Node inspect_node = inspect::Node());
  virtual ~SemanticTree() = default;

  // The two methods below set the handlers for dealing with assistive technology requests.
  void set_action_handler(ActionHandlerCallback action_handler) {
    action_handler_ = std::move(action_handler);
  }
  void set_hit_testing_handler(HitTestingHandlerCallback hit_testing_handler) {
    hit_testing_handler_ = std::move(hit_testing_handler);
  }

  // Sets callback invoked on semantics events.
  void set_semantics_event_callback(SemanticsEventCallback semantics_event_callback) {
    semantics_event_callback_ = std::move(semantics_event_callback);
  }

  // Returns the node with |node_id|, nullptr otherwise.
  const fuchsia::accessibility::semantics::Node* GetNode(const uint32_t node_id) const;

  // Returns the next node for which |filter| returns true (in depth first manner) from the node
  // with |node_id|, or nullptr if none exists.
  virtual const fuchsia::accessibility::semantics::Node* GetNextNode(
      const uint32_t node_id,
      fit::function<bool(const fuchsia::accessibility::semantics::Node*)> filter) const;

  // Returns the previous node for which |filter| returns true (in depth first manner) from the node
  // with |node_id|, or nullptr if none exists.
  virtual const fuchsia::accessibility::semantics::Node* GetPreviousNode(
      const uint32_t node_id,
      fit::function<bool(const fuchsia::accessibility::semantics::Node*)> filter) const;

  // Returns the parent node of the node with |node_id| if found, nullptr otherwise.
  virtual const fuchsia::accessibility::semantics::Node* GetParentNode(
      const uint32_t node_id) const;

  // Applies the node updates in |updates| if they leave the final resulting
  // tree in a valid state, returning true if the operation was successful. If
  // the updates leave the resulting tree in an invalid state, the updates are
  // not applied and this method returns false.
  //
  // A node can be either a full or a partial node. A full node is one where all
  // its values are filled. A partial node omits some field values, and is
  // intended to be merged with an existing one.
  //
  // Updates can be loosely classified in three main categories:
  // 1. Insertions: a new full node is being added with an node id that isn't
  // present on the tree.
  // 2. Node partial update: a node which already exists on the tree is being
  // updated with new information. The new node is merged with the old one such
  // as all fields are copied from the new node if present, keeping the old ones
  // if not.
  // 3. Deletion: a node is marked to be deleted from the tree.
  virtual bool Update(TreeUpdates updates);

  // Clears all nodes of the tree.
  void Clear();

  // Returns the number of nodes in this tree.
  size_t Size() const { return nodes_.size(); }

  // Performs accessibility action on this tree. This request is passed to
  // |action_handler_| which is received at construction time.
  void PerformAccessibilityAction(
      uint32_t node_id, fuchsia::accessibility::semantics::Action action,
      fuchsia::accessibility::semantics::SemanticListener::OnAccessibilityActionRequestedCallback
          callback) const;

  // Performs hit testing on this tree. This request is passed to
  // |hit_testing_handler_| which is received at construction time.
  void PerformHitTesting(
      fuchsia::math::PointF local_point,
      fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) const;

  // Debug aid.
  // - Do not rely on the concrete format for testing or any other purpose.
  // - Do not rely on this function being runtime efficient; it is not guaranteed to be.
  std::string ToString() const;

 private:
  // Validates the state of the resulting tree if the pending updates in |nodes_to_be_updated_| were
  // added. Note that the updates are not commited until ApplyNodeUpdates() is called.
  bool ValidateUpdate(std::unordered_set<uint32_t>* visited_nodes);

  // Adds |node| to the list of nodes to be updated. If |node| is already
  // in the tree, it is merged before being added to the list.
  void MarkNodeForUpdate(fuchsia::accessibility::semantics::Node node);

  // Applies nodes in |nodes_to_be_updated_| in the tree. |visited_nodes| contains all
  // nodes that are reachable from the root, and should be considered for an
  // update. If a node is present in the tree but not in |visited_nodes|, it
  // gets deleted, as it is not reachable anymore.
  void ApplyNodeUpdates(const std::unordered_set<uint32_t>& visited_nodes);

  // Returns true if a node has a label OR represents a button.
  // Returns false otherwise.
  bool NodeIsDescribable(const fuchsia::accessibility::semantics::Node* node) const;

  // Keeps all node updates to this tree which were not applied yet. Nodes are
  // just copied to their final location in the tree once a validation confirms
  // a well-formed tree. This also serves as an optimization to reset a tree
  // state that is not valid, by simply not applying this list of updates. An
  // optional value indicates that the node is a partial update, where a empty
  // one indicates a delletion.
  std::unordered_map<uint32_t, std::optional<fuchsia::accessibility::semantics::Node>>
      nodes_to_be_updated_;

  // Nodes from this tree. If not empty, there must be a node which node id is
  // equal to kRootNodeId. It is also garanteed that this tree is always valid.
  std::unordered_map<uint32_t /*node_id*/, fuchsia::accessibility::semantics::Node> nodes_;

  // Handler responsible for answering calls to PerformAccessibilityAction().
  ActionHandlerCallback action_handler_;

  // Handler responsible for answering calls to PerformHitTesting().
  HitTestingHandlerCallback hit_testing_handler_;

  // Callback invoked on semantics events.
  SemanticsEventCallback semantics_event_callback_;

  // Inpsect node to which to publish debug info.
  inspect::Node inspect_node_;

  // Number of updates received.
  uint64_t update_count_ = 0;

  // Inspect property to store the number of updates received.
  inspect::UintProperty inspect_property_update_count_;

  // Inspect node to store a dump of the semantic tree.
  inspect::LazyNode inspect_node_tree_dump_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_SEMANTIC_TREE_H_
