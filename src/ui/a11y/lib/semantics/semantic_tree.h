// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_SEMANTIC_TREE_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_SEMANTIC_TREE_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/math/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/vfs/cpp/pseudo_file.h>

#include <unordered_map>
#include <unordered_set>

#include <src/lib/fxl/macros.h>

#include "src/ui/a11y/lib/util/util.h"
#include "src/ui/scenic/lib/gfx/util/unwrap.h"

namespace a11y {

class SemanticTree : public fuchsia::accessibility::semantics::SemanticTree {
 public:
  // Callback which will be used to notify that an error is encountered while trying to apply the
  // commit.
  using CommitErrorCallback = fit::function<void(zx_koid_t)>;

  SemanticTree(fuchsia::ui::views::ViewRef view_ref,
               fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener,
               vfs::PseudoDir* debug_dir, CommitErrorCallback callback);

  ~SemanticTree() override;

  // Provides a way to query a node with node id. This method returns
  // a copy of the queried node. It may return a nullptr if no node is found.
  fuchsia::accessibility::semantics::NodePtr GetAccessibilityNode(uint32_t node_id);

  // Asks the semantics provider to perform an accessibility action on the
  // node with node id in the front-end.
  void OnAccessibilityActionRequested(
      uint32_t node_id, fuchsia::accessibility::semantics::Action action,
      fuchsia::accessibility::semantics::SemanticListener::OnAccessibilityActionRequestedCallback
          callback);

  // Compares a view with the current view of the semantic tree, based on KOID.
  bool IsSameView(const fuchsia::ui::views::ViewRef& view_ref);

  // Compares given koid with the koid of the current view's viewref.
  bool IsSameKoid(const zx_koid_t koid);

  // Calls HitTest() function for the current semantic tree with given local
  // point.
  void PerformHitTesting(
      ::fuchsia::math::PointF local_point,
      fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback);

  // Calls OnSemanticsModeChanged() to notify semantic provider whether Semantics Manager is enabled
  // or not.
  // Also, deletes the semantic tree, when Semantics Manager is disabled.
  void EnableSemanticsUpdates(bool enabled);

 private:
  // Representation of single semantic tree update/delete transaction.
  struct SemanticTreeTransaction {
    uint32_t node_id;
    bool delete_node;
    fuchsia::accessibility::semantics::Node node;
  };

  // Semantic Tree for a particular view. Each client is responsible for
  // maintaining the state of their tree. Nodes can be added, updated or
  // deleted. Because the size of an update may exceed FIDL transfer limits,
  // clients are responsible for breaking up changes into multiple update
  // and delete calls that conform to these limits. The commit function must
  // always be called at the end of a full update push to signal the end of
  // an update.
  // |fuchsia::accessibility::semantics::SemanticsTree|
  void Commit() override {}

  // Semantic Tree for a particular view. Each client is responsible for
  // maintaining the state of their tree. Nodes can be added, updated or
  // deleted. Because the size of an update may exceed FIDL transfer limits,
  // clients are responsible for breaking up changes into multiple update
  // and delete calls that conform to these limits. The commit function must
  // always be called at the end of a full update push to signal the end of
  // an update.
  // Updates/Deletes are processed in the order in which they are recieved. If the committed updates
  // result in an ill formed tree(for example a missing root node or a cycle) then semantic manager
  // will close the connection.
  // |fuchsia::accessibility::semantics::SemanticsTree|
  void CommitUpdates(CommitUpdatesCallback callback) override;

  // Semantic Tree supports partial updates of existing nodes. Clients should ensure that every node
  // in the list of nodes contains a node-id.
  // If node-id is missing, then semantic manager will ignore that node.
  // |fuchsia::accessibility::semantics::SemanticsTree|
  void UpdateSemanticNodes(std::vector<fuchsia::accessibility::semantics::Node> nodes) override;

  // |fuchsia::accessibility::semantics::SemanticsTree|
  void DeleteSemanticNodes(std::vector<uint32_t> node_ids) override;

  // Create semantic tree logs in a human readable form.
  std::string LogSemanticTree();

  // Helper function to traverse semantic tree with a root node, and for
  // creating string with tree information.
  void LogSemanticTreeHelper(fuchsia::accessibility::semantics::NodePtr root_node,
                             int current_level, std::string* tree_log);

  // Detect directed and undirected cycles in the tree rooted at "node".
  bool IsCyclic(fuchsia::accessibility::semantics::NodePtr node,
                std::unordered_set<uint32_t>* visited);

  // Helper function to delete subtree rooted at node_id.
  void DeleteSubtree(uint32_t node_id);

  // Helper function to delete pointer from parent node to given node.
  void DeletePointerFromParent(uint32_t node_id);

  // Internal helper function to check if a point is within a bounding box.
  static bool BoxContainsPoint(const fuchsia::ui::gfx::BoundingBox& box, const escher::vec2& point);

  // Function to create per view Log files under debug directory for debugging
  // semantic tree.
  void InitializeDebugEntry();

  // Helper function for applying commit.
  bool ApplyCommit();

  // Helper function to partially update fields from input node to output_node.
  static fuchsia::accessibility::semantics::NodePtr UpdateNode(
      fuchsia::accessibility::semantics::Node input_node,
      fuchsia::accessibility::semantics::NodePtr output_node);

  // List of committed, cached nodes for each front-end. We represent semantics
  // tree as a map of local node ids to the actual node objects. All query
  // operations should use the node information from these trees.
  std::unordered_map<uint32_t /*node_id*/, fuchsia::accessibility::semantics::Node> nodes_;

  // List of pending semantic tree transactions.
  std::vector<SemanticTreeTransaction> pending_transactions_;

  // This will be used to close the channel, if there is any issue while applying the commit.
  CommitErrorCallback commit_error_callback_;

  fuchsia::ui::views::ViewRef view_ref_;
  fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener_;
  vfs::PseudoDir* const debug_dir_;
  bool semantics_manager_enabled_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(SemanticTree);
};
}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_SEMANTIC_TREE_H_
