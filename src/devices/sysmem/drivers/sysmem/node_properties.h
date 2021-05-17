// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_NODE_PROPERTIES_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_NODE_PROPERTIES_H_

#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <lib/fit/function.h>
#include <stdint.h>

#include <memory>
#include <unordered_map>

#include <fbl/ref_ptr.h>

#include "logging.h"
#include "table_holder.h"

namespace sysmem_driver {

// ClientDebugInfo carries debug-specific information that can be attached to a Node, either by a
// participant, or using values inherited from the parent Node, or default values established when
// the root Node is created.
struct ClientDebugInfo {
  std::string name;
  zx_koid_t id{};
};

// ErrorPropagationMode
//
// The ErrorPropagationMode controls propagation of failure up the Node tree (failures always
// propagate down the Node tree), and also controls how much of the Node tree is involved in initial
// allocation, and also how much of the Node tree is involved in subsequent logical allocations.
// The granularity of subsequent logical allocations is designed to mimic the behavior of initial
// allocation, so that a given Node/participant connection sees the same allocation granularity
// with respect to SetDispensable() or AttachToken() sub-trees regardless of whether the Node itself
// is involved in initial allocation or a later logical allocation.
//
// The ErrorPropagationMode of a BufferCollectionToken / BufferCollection doesn't imply anything
// about the ErrorPropagationMode of its parent or children.
//
// SetDispensable() results in kPropagateBeforeAllocation.
//
// AttachToken() results in kDoNotPropagate.
//
// Failure of a BufferCollectionToken / BufferCollection will fail all its children, and will fail
// its immediate parent if ErrorPropagationMode is kPropagate or if ErrorPropagationMode is
// kPropagateBeforeAllocation and allocation (or logical allocation) has not yet occurred.
//
// Initial allocation will aggregate constraints of all nodes from the root down, with the exception
// of any sub-trees rooted at a kDoNotPropagate node.
//
// A sub-tree rooted at a kDoNotPropagate node will not aggregate its constraints into initial
// allocation.
//
// A sub-tree rooted at a kDoNotPropagate node, with a further sub-tree that's kDoNotPropagate, will
// separately aggregate the parent portion and succeed or fail logical allocation of that portion,
// then separately aggregate the kDoNotPropagate sub-tree and succeed or fail that portion.  This
// maximizes behavior simimlarity between a root with a kDoNotPropagate sub-tree and a
// kDoNotPropagate sub-tree with a further kDoNotPropagate sub-tree.
enum class ErrorPropagationMode : uint32_t {
  // On child failure, always fail the parent.  This is the mode of a token created via
  // fuchsia.sysmem.Allocator.AllocateSharedCollection() (the root), and the initial mode of a token
  // created via fuchsia.sysmem.BufferCollectionToken.Duplicate().
  kPropagate,
  // On child failure, fail the parent only if initial allocation has not yet occurred.  This is the
  // mode of a token after SetDispensable() on that token (unless the token was already
  // kDoNotPropagate, in which case it's still kDoNotPropagate).
  kPropagateBeforeAllocation,
  // Never fail the parent.  This is the mode of a token created with AttachToken().
  kDoNotPropagate,
};

struct NodeFilterResult {
  bool keep_node = true;
  bool iterate_children = true;
};

class Node;
class LogicalBufferCollection;

// The NodeProperties are properties that are not specific to whether the node is presently a
// live BufferCollectionToken, live BufferCollection, or just a raw non-live NodeProperties in
// orphaned_constraints_.
//
// This struct stays allocated as a BufferCollectionToken changes into a BufferCollection.  The
// node pointer is updated during that conversion, as the TreeNode interface is implemented by
// BufferCollectionToken and BufferCollection separately.
//
// Things that can change when transmuting from BufferCollectionToken to BufferCollection, from
// BufferCollectionToken to OrphanedNode, or from BufferCollection to OrphanedNode, should generally
// go in Node.  Things that don't change when transmuting go in NodeProperties.
class NodeProperties {
 public:
  // We keep pointers to NodeProperties around, so no copying or moving.
  NodeProperties(const NodeProperties& to_copy) = delete;
  NodeProperties(NodeProperties&& to_move) = delete;
  ~NodeProperties();

  // These are the only ways for client code to create a new NodeProperties.  These enforce that
  // NodeProperties are to be lifetime-managed using std::unique_ptr<NodeProperties>.  This is part
  // of preserving linkages from child NodeProperties to parent NodeProperties using a
  // NodeProperties*, since the child Node existing doesn't keep the parent alive.
  static std::unique_ptr<NodeProperties> NewRoot(
      LogicalBufferCollection* logical_buffer_collection);
  // The returned NodeProperties is already linked into the tree, and owned by the tree, so this
  // method just returns a raw pointer so we can inform the Node of its NodeProperties.
  NodeProperties* NewChild(LogicalBufferCollection* logical_buffer_collection);
  // Only for LogicalBufferCollection to use for temporary internal constraints.  We still enforce
  // that all instances of NodeProperties are managed by std::unique_ptr<NodeProperties> for
  // consistency.
  static std::unique_ptr<NodeProperties> NewTemporary(
      LogicalBufferCollection* logical_buffer_collection,
      fuchsia_sysmem2::wire::BufferCollectionConstraints buffer_collection_constraints,
      std::string debug_name);

  // Remove this NodeProperties from the tree by unlinking this NodeProperties from its parent,
  // which in turn will delete this NodeProperties, and also delete the corresponding Node.
  //
  // This call requires that this NodeProperties has zero children.
  void RemoveFromTreeAndDelete();

  // With default parameters, this returns a list of all the TreeNodeLinkage(s) starting at this
  // node as root, in breadth-first order, which can be used to Fail() all the nodes including this
  // node, by working from the back to the front of the list.  This breadth-first order is generated
  // without stack recursion, and Fail() from back to front of the returned vector also doesn't
  // involve stack recursion.
  //
  // If a node_filter is provided, and returns false for a given node, that node and the children of
  // that node are skipped.
  //
  // The default node_filter matches all nodes.
  std::vector<NodeProperties*> BreadthFirstOrder(
      fit::function<NodeFilterResult(const NodeProperties&)> node_filter =
          fit::function<NodeFilterResult(const NodeProperties&)>());

  NodeProperties* parent() const;
  Node* node() const;
  uint32_t child_count() const;

  ClientDebugInfo& client_debug_info();
  const ClientDebugInfo& client_debug_info() const;
  uint32_t& rights_attenuation_mask();
  ErrorPropagationMode& error_propagation_mode();
  const ErrorPropagationMode& error_propagation_mode() const;

  bool buffers_logically_allocated() const;
  void SetBuffersLogicallyAllocated();

  // BufferCollectionToken never has constraints yet, so returns nullptr.
  // BufferCollection may have constraints.
  // OrphanedConstraints may have constraints.
  bool has_constraints() const;
  const fuchsia_sysmem2::wire::BufferCollectionConstraints* buffer_collection_constraints() const;
  void SetBufferCollectionConstraints(
      TableHolder<fuchsia_sysmem2::wire::BufferCollectionConstraints>
          buffer_collection_constraints);

  void SetNode(fbl::RefPtr<Node> node);

  // These counts are for the current NodeProperties + any current children of the current
  // NodeProperties.  For LogicalBufferCollection::root_, these counts are for the whole tree.
  //
  // TODO(fxbug.dev/71454): Limit node_count() of root_, but instead of failing root_ when limit
  // reached, prune a sub-tree selected to prefer more-nested over less nested, and larger node
  // count over smaller node count (lexicographically).
  uint32_t node_count() const;
  uint32_t connected_client_count() const;
  uint32_t buffer_collection_count() const;
  uint32_t buffer_collection_token_count() const;

  void LogInfo(Location location, const char* format, ...) const __PRINTFLIKE(3, 4);

  // For debugging.
  void LogConstraints(Location location);

 private:
  friend class LogicalBufferCollection;
  explicit NodeProperties(LogicalBufferCollection* logical_buffer_collection);

  LogicalBufferCollection* logical_buffer_collection_ = nullptr;

  // Node linkage.
  //
  // The node field is updated when a BufferCollectionToken is transformed into a BufferCollection,
  // and when/if a BufferCollection is transformed into an OrphanedNode.
  //
  // In contrast, any pointers to the NodeProperties structure (such as from child to parent) do not
  // need to be updated, because NodeProperties is allocated separately from the Node itself, and
  // NodeProperties doesn't deallocate or move when the Node changes from one type to another.
  NodeProperties* parent_ = nullptr;
  fbl::RefPtr<Node> node_;
  // We use shared_ptr<> instead of unique_ptr<> here only so that Node can keep a std::weak_ptr<>.
  // The only non-transient ownership of NodeProperties is by the tree at
  // LogicalBufferCollection::root_.
  std::unordered_map<NodeProperties*, std::shared_ptr<NodeProperties>> children_;

  ClientDebugInfo client_debug_info_{};

  // The rights attenuation mask driven by BufferCollectionToken::Duplicate()
  // rights_attenuation_mask parameter(s) as the token is duplicated,
  // potentially via multiple participants.
  //
  // 1 bit means the right is allowed.  0 bit means the right is attenuated.
  uint32_t rights_attenuation_mask_ = std::numeric_limits<uint32_t>::max();

  // In the absence of SetDispensable() and AttachToken(), only kPropagate mode is used.
  //
  // SetDispensable() results in kPropagateBeforeAllocation.
  //
  // AttachToken() results in kDoNotPropagate.
  ErrorPropagationMode error_propagation_mode_ = ErrorPropagationMode::kPropagate;

  bool buffers_logically_allocated_ = false;

  // Constraints as set by:
  //
  // v1:
  //     optional SetConstraintsAuxBuffers
  //     SetConstraints()
  //
  // v2 (TODO):
  //     SetConstraints()
  //
  // Either way, the constraints here are in v2 form.
  std::optional<TableHolder<fuchsia_sysmem2::wire::BufferCollectionConstraints>>
      buffer_collection_constraints_;

  // These counts are for the current NodeProperties + any current children of the current
  // NodeProperties.  For LogicalBufferCollection::root_, these counts are for the whole tree.
  uint32_t node_count_ = 0;
  uint32_t connected_client_count_ = 0;
  uint32_t buffer_collection_count_ = 0;
  uint32_t buffer_collection_token_count_ = 0;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_NODE_PROPERTIES_H_
