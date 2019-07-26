// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <blobfs/iterator/extent-iterator.h>
#include <blobfs/format.h>
#include <zircon/types.h>

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

 private:
  // Indicates if the current node is the inode (as opposed to a container).
  bool IsInode() const;

  // Returns |ZX_OK| if the container can accept another extent.
  zx_status_t ValidateExtentCount() const;

  // Moves the block, extent, and local indices forward.
  void UpdateIndices(const Extent& extent);

  // Acquires the current extent.
  const Extent* GetExtent() const;

  // Acquires the index of the next node in the chain of nodes.
  uint32_t GetNextNode() const;

  // Moves from either an inode to a container, or from one container to another.
  //
  // Returns an error if this container is unallocated, or not marked as a container.
  zx_status_t NextContainer();

  NodeFinder* finder_;
  Inode* inode_;
  // The index of the node we're currently observing.
  uint32_t node_index_;
  ExtentContainer* extent_node_ = nullptr;
  // The block index, indicating how many blocks we've iterated past thus far.
  uint64_t block_index_ = 0;
  // The extent index into the global inode (monotonically increases).
  uint32_t extent_index_ = 0;
  // The extent index into the current inode or container.
  uint32_t local_index_ = 0;
};

}  // namespace blobfs
