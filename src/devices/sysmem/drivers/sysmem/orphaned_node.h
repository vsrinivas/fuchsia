// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_ORPHANED_NODE_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_ORPHANED_NODE_H_

#include "node.h"

namespace sysmem_driver {

// An OrphanedNode keeps the place of a former BufferCollectionToken or BufferCollection in the
// hierarchical failure domain tree.  This way we don't need to adjust the tree due to disappearing
// Node; instead we just replace with an OrphanedNode.  The OrphanedNode also preserves the
// error_propagation_mode(), and avoids needing to check for Node absence in several places.  The
// OrphanedNode also preserves BufferCollectionConstraints of a former BufferCollection when
// applicable.
//
// The only way an OrphanedNode can exist is if Close() was used on the BufferCollectionToken or
// BufferCollection, because otherwise the sub-tree (or whole tree) fails, which removes the nodes
// in that sub-tree (or whole tree).
class OrphanedNode : public Node {
 public:
  static OrphanedNode& EmplaceInTree(fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection,
                                     NodeProperties* node_properties);

  // Node interface
  bool ReadyForAllocation() override;

  void OnBuffersAllocated(const AllocationResult& allocation_result) override;
  BufferCollectionToken* buffer_collection_token() override;
  const BufferCollectionToken* buffer_collection_token() const override;
  BufferCollection* buffer_collection() override;
  const BufferCollection* buffer_collection() const override;
  BufferCollectionTokenGroup* buffer_collection_token_group() override;
  const BufferCollectionTokenGroup* buffer_collection_token_group() const override;
  OrphanedNode* orphaned_node() override;
  const OrphanedNode* orphaned_node() const override;
  bool is_connected_type() const override;
  bool is_currently_connected() const override;
  const char* node_type_string() const override;

 protected:
  // OrphanedNode::Bind() will never happen; panics.
  void BindInternal(zx::channel server_end, ErrorHandlerWrapper error_handler_wrapper) override;
  // NOP
  void CloseServerBinding(zx_status_t epitaph) override;

 private:
  OrphanedNode(fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection,
               NodeProperties* node_properties);
  void SetErrorHandler(fit::function<void(zx_status_t)> error_handler) = delete;
  bool is_done() const = delete;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_ORPHANED_NODE_H_
