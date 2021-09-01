// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_NODE_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_NODE_H_

#include <fidl/fuchsia.sysmem2/cpp/wire.h>
#include <stdint.h>
#include <zircon/types.h>

#include <unordered_set>
#include <vector>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "allocation_result.h"

namespace sysmem_driver {

class NodeProperties;
class BufferCollection;
class BufferCollectionToken;
class LogicalBufferCollection;
class OrphanedNode;

// Implemented by BufferCollectionToken, BufferCollection, and OrphanedNode.
//
// Things that can change when transmuting from BufferCollectionToken to BufferCollection, from
// BufferCollectionToken to OrphanedNode, or from BufferCollection to OrphanedNode, should generally
// go in Node.  Things that don't change when transmuting go in NodeProperties.
class Node : public fbl::RefCounted<Node> {
 public:
  Node(const Node& to_copy) = delete;
  Node(Node&& to_move) = delete;

  Node(fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection,
       NodeProperties* node_properties);
  virtual ~Node();

  // Not all Node(s) that are ReadyForAllocation() have buffer_collection_constraints().  In
  // particular an OrphanedNode is always ReadyForAllocation(), but may or may not have
  // buffer_collection_constraints().
  virtual bool ReadyForAllocation() = 0;

  // buffers_logically_allocated() must be false to call this.
  virtual void OnBuffersAllocated(const AllocationResult& allocation_result) = 0;

  // The TreeNode must have 0 children to call Fail().
  virtual void Fail(zx_status_t epitaph) = 0;

  // If this Node is a BufferCollectionToken, returns the BufferCollectionToken*, else returns
  // nullptr.
  virtual BufferCollectionToken* buffer_collection_token() = 0;
  virtual const BufferCollectionToken* buffer_collection_token() const = 0;
  // If this Node is a BufferCollection, returns the BufferCollection*, else returns nullptr.
  virtual BufferCollection* buffer_collection() = 0;
  virtual const BufferCollection* buffer_collection() const = 0;
  // If this Node is an OrphanedNode, returns the OrphanedNode*, else returns nullptr.
  virtual OrphanedNode* orphaned_node() = 0;
  virtual const OrphanedNode* orphaned_node() const = 0;
  // This is a constant per sub-class of Node.  When a "connected" node is no longer connected, the
  // Node sub-class is replaced with OrphanedNode, or deleted as appropriate.
  virtual bool is_connected() const = 0;

  LogicalBufferCollection& logical_buffer_collection() const;
  fbl::RefPtr<LogicalBufferCollection> shared_logical_buffer_collection();

  // If the NodeProperties this Node started with is gone, this asserts, including in release.  A
  // hard crash is better than going off in the weeds.
  NodeProperties& node_properties() const;

  void EnsureDetachedFromNodeProperties();

 private:
  // This is in Node instead of NodeProperties because when BufferCollectionToken or
  // BufferCollection becomes an OrphanedNode, we no longer reference LogicalBufferCollection.
  fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection_;
  // The Node is co-owned by the NodeProperties, so the Node has a raw pointer back to
  // NodeProperties.
  //
  // This pointer is set to nullptr during ~NodeProperties(), so if we attempt to access via
  // node_properties_ after that, we'll get a hard crash instead of going off in the weeds.
  //
  // The main way we avoid accessing NodeProperties beyond when it goes away is the setting of
  // error_handler_ = {} in the two CloseChannel() methods.  We rely on sub-class's error_handler_
  // not running after CloseChannel(), and we rely on LLCPP not calling protocol message handlers
  // after server binding Close() (other than completion of any currently-in-progress message
  // handler), since we're running Close() on the same dispatcher.
  NodeProperties* node_properties_ = nullptr;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_NODE_H_
