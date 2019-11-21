// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_SEMANTIC_TREE_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_SEMANTIC_TREE_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/vfs/cpp/pseudo_file.h>

#include <unordered_map>
#include <unordered_set>

#include "src/lib/fxl/macros.h"
#include "src/ui/a11y/lib/util/util.h"

namespace a11y {

class SemanticTree : public fuchsia::accessibility::semantics::SemanticTree {
 public:
  // Callback which will be used to notify that an error is encountered while trying to apply the
  // commit.
  using CloseChannelCallback = fit::function<void(zx_koid_t)>;

  SemanticTree(fuchsia::ui::views::ViewRef view_ref,
               fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener,
               vfs::PseudoDir* debug_dir, CloseChannelCallback callback);

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

  // Semantic Tree for a particular view. Each client is responsible for
  // maintaining the state of their tree. Nodes can be added, updated or
  // deleted. Because the size of an update may exceed FIDL transfer limits,
  // clients are responsible for breaking up changes into multiple update
  // and delete calls that conform to these limits. The commit function must
  // always be called at the end of a full update push to signal the end of
  // an update.
  // Updates/Deletes are processed in the order in which they are received. If the committed
  // updates result in an ill formed tree(for example a tree containing a cycle) then semantic
  // manager will close the connection. |fuchsia::accessibility::semantics::SemanticsTree|
  void CommitUpdates(CommitUpdatesCallback callback) override;

  // Semantic Tree supports partial updates of existing nodes. Clients should ensure that every
  // node in the list of nodes contains a node-id.
  // If node-id is missing, then semantic manager will ignore that node.
  // |fuchsia::accessibility::semantics::SemanticsTree|
  void UpdateSemanticNodes(std::vector<fuchsia::accessibility::semantics::Node> nodes) override;

  // |fuchsia::accessibility::semantics::SemanticsTree|
  void DeleteSemanticNodes(std::vector<uint32_t> node_ids) override;

  // TODO(fxb/40132): Remove test-only methods, and move some of intermediate state into a separate
  // class.

  // The methods below labelled "FOR TESTING ONLY" are used to manipulate/observe internal tree
  // state directly for testing purposes.

  // FOR TESTING ONLY
  // Create semantic tree logs in a human readable form. This method enables us to verify the
  // state of the semantic tree against static golden text.
  std::string LogSemanticTree();

  // FOR TESTING ONLY
  // Populates nodes_. This method is helpful for setting up the semantic tree without using
  // UpdateSemanticNodes(), which enables us to create certain edge/error states in the tree
  // that the update API would otherwise prevent.
  void InitializeNodesForTest(std::vector<fuchsia::accessibility::semantics::Node> nodes);

  // FOR TESTING ONLY
  // Add pending transaction. This method is useful, because it enables us to test
  // CommitUpdates() independently of UpdateSemanticNodes() and DeleteSemanticNodes().
  void AddPendingTransaction(const uint32_t node_id, bool delete_node,
                             fuchsia::accessibility::semantics::Node node);

  // FOR TESTING ONLY
  // Returns a list of node ids to be deleted. This method enables us to verify that
  // DeleteSemanticNodes() correctly alters the state of pending_transactions_.
  std::vector<uint32_t> GetPendingDeletions();

  // FOR TESTING ONLY
  // Returns a list of node updates. This method enables us to verify that UpdateSemanticNodes()
  // correctly alters the state of pending_transactions_.
  std::vector<fuchsia::accessibility::semantics::Node> GetPendingUpdates();

 private:
  // Representation of single semantic tree update/delete transaction.
  struct SemanticTreeTransaction {
    uint32_t node_id;
    bool delete_node;
    fuchsia::accessibility::semantics::Node node;
  };

  // Helper function to traverse semantic tree with a root node, and for
  // creating string with tree information.
  void LogSemanticTreeHelper(fuchsia::accessibility::semantics::NodePtr root_node,
                             int current_level, std::string* tree_log);

  // Detect directed and undirected cycles in the tree rooted at "node".
  // For a tree to have cycles there should be at least one node which should have multiple parents.
  // And because of multiple parents it will be visited twice through different paths.
  // In a tree without cycles every node should have just 1 path from root node.
  bool IsTreeWellFormed(fuchsia::accessibility::semantics::NodePtr node,
                        std::unordered_set<uint32_t>* visited);

  // Checks if there are multiple disjoint subtrees in the semantic tree. In other words it
  // ensures that every node is reachable from the root node. This function uses "visited"
  // flags(which was created while checking cycles in the tree) to see which nodes are
  // unreachable.
  bool CheckIfAllNodesReachable(const std::unordered_set<uint32_t>& visited);

  // Internal helper function to check if a point is within a bounding box.
  static bool BoxContainsPoint(const fuchsia::ui::gfx::BoundingBox& box,
                               const fuchsia::math::PointF& point);

  // Function to create per view Log files under debug directory for debugging
  // semantic tree.
  void InitializeDebugEntry();

  // Helper function for applying commit.
  bool ApplyCommit();

  // SignalHandler is called when ViewRef peer is destroyed. It is responsible for closing the
  // channel.
  void SignalHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                     const zx_packet_signal* signal);

  // Helper function to check if the node with node_id exists in the semantic tree. If the
  // node is present then it returns true.
  bool NodeExists(const fuchsia::accessibility::semantics::NodePtr& node_ptr, uint32_t node_id);

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

  // This will be used to close the channel, if there is any error.
  CloseChannelCallback close_channel_callback_;

  fuchsia::ui::views::ViewRef view_ref_;
  async::WaitMethod<SemanticTree, &SemanticTree::SignalHandler> wait_;
  fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener_;
  vfs::PseudoDir* const debug_dir_;
  bool semantics_manager_enabled_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(SemanticTree);
};
}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_SEMANTIC_TREE_H_
