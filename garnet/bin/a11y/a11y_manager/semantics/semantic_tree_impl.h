// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_A11Y_MANAGER_SEMANTICS_SEMANTIC_TREE_IMPL_H_
#define GARNET_BIN_A11Y_A11Y_MANAGER_SEMANTICS_SEMANTIC_TREE_IMPL_H_

#include <unordered_map>

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/math/cpp/fidl.h>
#include <garnet/lib/ui/gfx/util/unwrap.h>
#include <lib/fidl/cpp/binding_set.h>
#include <src/lib/fxl/macros.h>

namespace a11y_manager {

class SemanticTreeImpl
    : public fuchsia::accessibility::semantics::SemanticTree {
 public:
  explicit SemanticTreeImpl(
      zx::event view_ref,
      fuchsia::accessibility::semantics::SemanticActionListenerPtr
          client_action_listener)
      : view_ref_(std::move(view_ref)),
        client_action_listener_(std::move(client_action_listener)) {}

  ~SemanticTreeImpl() override = default;

  // Provides a way to perform hit-testing for a front-end node with local view
  // hit coordinates from Scenic. Returns the deepest node that the input
  // touches. Currently, this only supports 2D hit-tests using bounding boxes.
  fuchsia::accessibility::semantics::NodePtr GetHitAccessibilityNode(
      fuchsia::math::PointF point);

  // Provides a way to query a node with node id. This method returns
  // a copy of the queried node. It may return a nullptr if no node is found.
  fuchsia::accessibility::semantics::NodePtr GetAccessibilityNode(
      uint32_t node_id);

  // Asks the semantics provider to perform an accessibility action on the
  // node with node id in the front-end.
  void OnAccessibilityActionRequested(
      uint32_t node_id, fuchsia::accessibility::semantics::Action action,
      fuchsia::accessibility::semantics::SemanticActionListener::
          OnAccessibilityActionRequestedCallback callback);

 private:
  // Semantic Tree for a particular view. Each client is responsible for
  // maintaining the state of their tree. Nodes can be added, updated or
  // deleted. Because the size of an update may exceed FIDL transfer limits,
  // clients are responsible for breaking up changes into multiple update
  // and delete calls that conform to these limits. The commit function must
  // always be called at the end of a full update push to signal the end of
  // an update.
  // |fuchsia::accessibility::semantics::SemanticsTree|
  void Commit() override;

  // |fuchsia::accessibility::semantics::SemanticsTree|
  void UpdateSemanticNodes(
      std::vector<fuchsia::accessibility::semantics::Node> nodes) override;

  // |fuchsia::accessibility::semantics::SemanticsTree|
  void DeleteSemanticNodes(std::vector<uint32_t> node_ids) override;

  // Function for logging semantic tree.
  void LogSemanticTree();

  // Helper function to traverse semantic tree with a root node, and for
  // creating string with tree information.
  void LogSemanticTreeHelper(
      fuchsia::accessibility::semantics::NodePtr root_node, int current_level,
      std::string* tree_log);

  // Internal recursive hit-test function using the cached tree. Returns a
  // null pointer if no hit nodes were found. Returns a copy of the node
  // (but not the subtree), to prevent tree modification.
  // NOTE: This is a 2D hit test and only operates on bounding boxes of
  // semantics nodes.
  const fuchsia::accessibility::semantics::NodePtr HitTest(
      const std::unordered_map<uint32_t,
                               fuchsia::accessibility::semantics::Node>& nodes,
      uint32_t starting_node_id, escher::vec4 coordinates) const;

  // Internal helper function to check if a point is within a bounding box.
  bool BoxContainsPoint(const fuchsia::ui::gfx::BoundingBox& box,
                        const escher::vec2& point) const;

  // List of committed, cached nodes for each front-end. We represent semantics
  // tree as a map of local node ids to the actual node objects. All query
  // operations should use the node information from these trees.
  std::unordered_map<uint32_t /*node_id*/,
                     fuchsia::accessibility::semantics::Node>
      nodes_;

  // List of nodes that should be updated or added to the
  // tree on the next commit.
  std::vector<fuchsia::accessibility::semantics::Node> uncommitted_nodes_;

  // List of local node ids that should be removed from the next commit.
  std::vector<uint32_t> /*node_ids*/ uncommitted_deletes_;

  zx::event view_ref_;
  fuchsia::accessibility::semantics::SemanticActionListenerPtr
      client_action_listener_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SemanticTreeImpl);
};
}  // namespace a11y_manager

#endif  // GARNET_BIN_A11Y_A11Y_MANAGER_SEMANTICS_SEMANTIC_TREE_IMPL_H_
