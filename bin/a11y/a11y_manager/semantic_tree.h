// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_A11Y_MANAGER_SEMANTIC_TREE_H_
#define GARNET_BIN_A11Y_A11Y_MANAGER_SEMANTIC_TREE_H_

#include <unordered_map>

#include <fuchsia/accessibility/cpp/fidl.h>

#include "garnet/lib/ui/gfx/util/unwrap.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace a11y_manager {

// Represents an aggregate semantics tree of all front-ends. Each front-end
// semantics tree is associated with a Scenic view id.
// To query a specific node from a particular front-end, we need to provide
// a Scenic view id to figure out which front-end semantics tree to query,
// and a node id to get the specific node from the tree.

class SemanticTree : public fuchsia::accessibility::SemanticsRoot {
 public:
  explicit SemanticTree() = default;
  ~SemanticTree() = default;

  // Allows the SemanticTree to handle client connection requests to its
  // SemanticRoot interface.
  void AddBinding(
      fidl::InterfaceRequest<fuchsia::accessibility::SemanticsRoot> request);

  // Provides the a11y manager with a way to perform hit-testing for a
  // front-end node when it has the view id and the local view hit
  // coordinates from Scenic. Currently, this only supports 2D hit-tests
  // using bounding boxes.
  fuchsia::accessibility::NodePtr GetHitAccessibilityNode(
      int32_t view_id, fuchsia::math::PointF point);

  // Provides the manager a way to query a node if it already knows
  // what view id and node id it wants to query for. This method returns
  // a copy of the queried node. It may return a nullptr if no node is found.
  fuchsia::accessibility::NodePtr GetAccessibilityNode(int32_t view_id,
                                                       int32_t node_id);

  // Since the SemanticsTree holds the references to the front-end semantics
  // providers, it must be the one to perform actions.
  void PerformAccessibilityAction(int32_t view_id, int32_t node_id,
                                  fuchsia::accessibility::Action action);

 private:
  // |fuchsia::accessibility::SemanticRoot|:

  // We tie the lifetime of the view id to to the lifetime of the
  // SemanticsProvider connection. Upon SemanticsProvider connection error,
  // we remove the associated view id semantics tree in the mappings.
  // Providers should re-register upon connection error to send more data.
  void RegisterSemanticsProvider(
      int32_t view_id,
      fidl::InterfaceHandle<fuchsia::accessibility::SemanticsProvider> handle)
      override;
  void UpdateSemanticNodes(
      int32_t view_id,
      fidl::VectorPtr<fuchsia::accessibility::Node> nodes) override;
  void DeleteSemanticNodes(int32_t view_id,
                           fidl::VectorPtr<int32_t> node_ids) override;
  void Commit(int32_t view_id) override;

  // Internal recursive hit-test function using the cached tree. Returns a
  // null pointer if no hit nodes were found. Public functions that query
  // nodes from the tree should always return a copy of the node to avoid
  // unintentional modification of the tree.
  // NOTE: This is a 2D hit test and only operates on bounding boxes of
  // semantics nodes.
  fuchsia::accessibility::Node* HitTest(
      std::unordered_map<int32_t, fuchsia::accessibility::Node>& nodes,
      int32_t node_id, escher::vec4 coordinates);

  fidl::BindingSet<fuchsia::accessibility::SemanticsRoot> bindings_;

  // When a front-end registers itself to the SemanticsRoot using
  // RegisterSemanticProvider, we create an entry in the following four
  // maps keyed to the front-end's view id. We follow a simple commit system
  // to provide a way for front-ends to send atomic updates to the
  // SemanticsRoot.

  // Maps view ids to the committed, cached trees for each front-end. For
  // each front-end, we represent its semantics tree as a map of local
  // node ids to the actual node objects. All query operations should
  // use the node information from these trees.
  std::unordered_map<
      int32_t /*view_id*/,
      std::unordered_map<int32_t /*node_id*/, fuchsia::accessibility::Node>>
      nodes_;

  // Maps view ids to the list of nodes that should be updated or added to the
  // tree on the next commit.
  std::unordered_map<int32_t /*view_id*/,
                     std::vector<fuchsia::accessibility::Node>>
      uncommitted_nodes_;

  // Maps view ids to the list of local node ids that should be removed from
  // the next commit.
  std::unordered_map<int32_t /*view_id*/, std::vector<int32_t> /*node_ids*/>
      uncommitted_deletes_;

  // Maps view ids to SemanticsProvider pointers that the SemanticTree
  // can use to ask front-ends to perform accessibility actions.
  std::unordered_map<int32_t /*view_id*/,
                     fuchsia::accessibility::SemanticsProviderPtr>
      providers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SemanticTree);
};

}  // namespace a11y_manager

#endif  // GARNET_BIN_A11Y_A11Y_MANAGER_SEMANTIC_TREE_H_
