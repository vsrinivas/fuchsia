// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_ITERATOR_ALLOCATED_EXTENT_ITERATOR_H_
#define SRC_STORAGE_BLOBFS_ITERATOR_ALLOCATED_EXTENT_ITERATOR_H_

#include <stdbool.h>
#include <stdint.h>
#include <zircon/types.h>

#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/iterator/allocated_node_iterator.h"
#include "src/storage/blobfs/iterator/extent_iterator.h"
#include "src/storage/blobfs/node_finder.h"

namespace blobfs {

// Allows traversing a collection of extents from an already-allocated node. Partially validates
// containers as they are traversed.
//
// This iterator is useful for accessing blobs which have already been written to disk.
class AllocatedExtentIterator : public ExtentIterator {
 public:
  AllocatedExtentIterator& operator=(const AllocatedExtentIterator&) = delete;
  AllocatedExtentIterator(const AllocatedExtentIterator&) = delete;
  AllocatedExtentIterator(AllocatedExtentIterator&&) = default;
  AllocatedExtentIterator& operator=(AllocatedExtentIterator&&) = default;

  // Creates an AllocatedExtentIterator.  Returns an error if |node_index| isn't a valid index in
  // |finder|.
  static zx::result<AllocatedExtentIterator> Create(NodeFinder* finder, uint32_t node_index);

  // ExtentIterator interface.
  bool Done() const final;
  zx::result<const Extent*> Next() final;
  uint64_t BlockIndex() const final;

  // Returns the number of extents we've iterated past already.
  uint32_t ExtentIndex() const;

  // Returns the node we're about to read from on the upcoming call to |Next|.
  //
  // It is unsafe to call this method if |Done()| is true.
  uint32_t NodeIndex() const;

  // Returns |ZX_OK| when the node list can be safely traversed.
  static zx_status_t VerifyIteration(NodeFinder* finder, uint32_t node_index, Inode* inode);

  // Returns the prelude for the current node.
  const NodePrelude& node_prelude() const {
    return extent_node_ ? extent_node_->header : inode_->header;
  }

 private:
  AllocatedExtentIterator(NodeFinder* finder, InodePtr inode, uint32_t node_index);

  // Indicates if the current node is the inode (as opposed to a container).
  bool IsInode() const;

  // Acquires the current extent.
  const Extent& GetExtent() const;

  // Moves from either an inode to a container, or from one container to another.
  //
  // Returns an error if this container is unallocated, or not marked as a container.
  zx_status_t NextContainer();

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
