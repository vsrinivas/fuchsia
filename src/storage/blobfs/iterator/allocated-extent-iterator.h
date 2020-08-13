// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_ITERATOR_ALLOCATED_EXTENT_ITERATOR_H_
#define SRC_STORAGE_BLOBFS_ITERATOR_ALLOCATED_EXTENT_ITERATOR_H_

#include <stdbool.h>
#include <stdint.h>
#include <zircon/types.h>

#include <blobfs/format.h>

#include "allocated-node-iterator.h"
#include "extent-iterator.h"

namespace blobfs {

// Allows traversing a collection of extents from an already-allocated node.
// Partially validates containers as they are traversed.
//
// This iterator is useful for accessing blobs which have already been written
// to disk.
class AllocatedExtentIterator : public ExtentIterator {
 public:
  AllocatedExtentIterator(NodeFinder* finder, uint32_t node_index);
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AllocatedExtentIterator);

  ////////////////
  // ExtentIterator interface.

  bool Done() const final;
  zx_status_t Next(const Extent** out) final;
  uint64_t BlockIndex() const final;

  ////////////////
  // Other methods.

  // Returns the number of extents we've iterated past already.
  uint32_t ExtentIndex() const;

  // Returns the node we're about to read from on the upcoming call to |Next|.
  //
  // It is unsafe to call this method if |Done()| is true.
  uint32_t NodeIndex() const;

  // Returns |ZX_OK| when the node list can be safely traversed.
  static zx_status_t VerifyIteration(NodeFinder* finder, Inode* inode);

 private:
  // Indicates if the current node is the inode (as opposed to a container).
  bool IsInode() const;

  // Returns |ZX_OK| if the container can accept another extent.
  zx_status_t ValidateExtentCount() const;

  // Moves the block, extent, and local indices forward.
  void UpdateIndices(const Extent& extent);

  // Acquires the current extent.
  const Extent* GetExtent() const;

  // Moves from either an inode to a container, or from one container to another.
  //
  // Returns an error if this container is unallocated, or not marked as a container.
  zx_status_t NextContainer();

  NodeFinder* finder_;
  InodePtr inode_;
  // The index of the node we're currently observing.
  uint32_t node_index_;
  AllocatedNodeIterator node_iterator_;
  ExtentContainer* extent_node_ = nullptr;
  // The block index, indicating how many blocks we've iterated past thus far.
  uint64_t block_index_ = 0;
  // The extent index into the current inode or container.
  uint32_t local_index_ = 0;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_ITERATOR_ALLOCATED_EXTENT_ITERATOR_H_
