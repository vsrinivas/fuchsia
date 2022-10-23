// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_ITERATOR_ALLOCATED_NODE_ITERATOR_H_
#define SRC_STORAGE_BLOBFS_ITERATOR_ALLOCATED_NODE_ITERATOR_H_

#include <lib/zx/result.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/types.h>

#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/node_finder.h"

namespace blobfs {

// Allows traversing a linked list of nodes for a single blob that has already been written to disk.
class AllocatedNodeIterator {
 public:
  AllocatedNodeIterator& operator=(const AllocatedNodeIterator&) = delete;
  AllocatedNodeIterator(const AllocatedNodeIterator&) = delete;
  AllocatedNodeIterator(AllocatedNodeIterator&&) = default;
  AllocatedNodeIterator& operator=(AllocatedNodeIterator&&) = default;

  AllocatedNodeIterator(NodeFinder* finder, uint32_t node_index, Inode* inode);

  // Returns true when there are no more nodes to traverse.
  bool Done() const;

  // Returns a pointer to the next extent container on success, or a status on
  // error.
  zx::result<ExtentContainer*> Next();

  // Returns the number of extents we've iterated past already.
  uint32_t ExtentIndex() const;

  // Returns the next node to be returned on the upcoming call to |Next|.
  // It is unsafe to call this method if |Done()| is true.
  uint32_t NextNodeIndex() const;

  uint32_t current_node_index() const { return current_node_index_; }

 private:
  // Number of extents in the current node.
  uint32_t NodeExtentCount() const;

  // Indicates if the current node is the inode (as opposed to a container).
  bool IsInode() const;

  NodeFinder* finder_;
  uint32_t current_node_index_ = 0;
  Inode* inode_;
  ExtentContainer* extent_node_ = nullptr;
  // The extent index into the global inode (monotonically increases).
  uint32_t extent_index_ = 0;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_ITERATOR_ALLOCATED_NODE_ITERATOR_H_
