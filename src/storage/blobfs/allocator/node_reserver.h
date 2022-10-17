// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_ALLOCATOR_NODE_RESERVER_H_
#define SRC_STORAGE_BLOBFS_ALLOCATOR_NODE_RESERVER_H_

#include <lib/zx/status.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/types.h>

#include <bitmap/rle-bitmap.h>

#include "src/storage/blobfs/format.h"

namespace blobfs {

class ReservedNode;

// Interface for reserving and unreserving nodes. The purpose of reservation is to allow allocation
// of nodes to occur without yet allocating structures which could be written out to durable
// storage.
class NodeReserverInterface {
 public:
  // Reserves space for a node in memory. Does not update disk. Returns an error if a node could not
  // be reserved.
  virtual zx::result<ReservedNode> ReserveNode() = 0;

  // Unreserves space for a node in memory. Does not update disk.
  virtual void UnreserveNode(ReservedNode node) = 0;

  // Returns the total number of reserved nodes.
  virtual uint64_t ReservedNodeCount() const = 0;
};

// Wraps a node reservation in RAII to hold the reservation active, and release it when it goes out
// of scope.
// Thread-compatible.
class ReservedNode {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ReservedNode);

  ReservedNode(NodeReserverInterface* reserver, uint32_t node);
  ReservedNode(ReservedNode&& o) noexcept;
  ReservedNode& operator=(ReservedNode&& o) noexcept;

  ~ReservedNode();

  // Access the underlying node index which has been reserved.
  //
  // Unsafe to call if the node has not actually been reserved.
  uint32_t index() const;

  // Update internal state such that future calls to |Reserved| return false.
  void Release();

 private:
  // Releases the underlying node, unreserving it and preventing continued access to |index()|.
  void Reset();

  // Identify if the underlying node is reserved, and able to be accessed.
  bool Reserved() const;

  NodeReserverInterface* reserver_;
  uint32_t node_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_ALLOCATOR_NODE_RESERVER_H_
