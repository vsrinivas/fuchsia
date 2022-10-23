// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/iterator/allocated_extent_iterator.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/result.h>
#include <stdint.h>
#include <zircon/types.h>

#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/iterator/allocated_node_iterator.h"
#include "src/storage/blobfs/iterator/extent_iterator.h"

namespace blobfs {

AllocatedExtentIterator::AllocatedExtentIterator(NodeFinder* finder, InodePtr inode,
                                                 uint32_t node_index)
    : inode_(std::move(inode)), node_iterator_(finder, node_index, inode_.get()) {}

zx::result<AllocatedExtentIterator> AllocatedExtentIterator::Create(NodeFinder* finder,
                                                                    uint32_t node_index) {
  auto inode = finder->GetNode(node_index);
  if (inode.is_error()) {
    return inode.take_error();
  }
  if (!inode->header.IsAllocated() || !inode->header.IsInode()) {
    FX_LOGS(ERROR) << "node_index " << node_index << " isn't a valid inode: " << *(inode.value());
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  return zx::ok(AllocatedExtentIterator(finder, std::move(inode.value()), node_index));
}

bool AllocatedExtentIterator::Done() const { return ExtentIndex() >= inode_->extent_count; }

zx::result<const Extent*> AllocatedExtentIterator::Next() {
  if (Done()) {
    return zx::error(ZX_ERR_BAD_STATE);
  }

  const Extent& extent = GetExtent();

  if (ExtentIndex() + 1 < inode_->extent_count &&
      local_index_ + 1 >= (IsInode() ? kInlineMaxExtents : extent_node_->extent_count)) {
    if (zx_status_t status = NextContainer(); status != ZX_OK) {
      return zx::error(status);
    }
  } else {
    ++local_index_;
  }

  block_index_ += extent.Length();

  return zx::ok(&extent);
}

uint64_t AllocatedExtentIterator::BlockIndex() const { return block_index_; }

uint32_t AllocatedExtentIterator::ExtentIndex() const {
  return local_index_ + node_iterator_.ExtentIndex();
}

uint32_t AllocatedExtentIterator::NodeIndex() const {
  ZX_DEBUG_ASSERT(!Done());
  return node_iterator_.current_node_index();
}

zx_status_t AllocatedExtentIterator::VerifyIteration(NodeFinder* finder, uint32_t node_index,
                                                     Inode* inode) {
  uint32_t container_count = 0;
  AllocatedNodeIterator fast(finder, node_index, inode);
  AllocatedNodeIterator slow(finder, node_index, inode);
  while (!fast.Done()) {
    zx::result<ExtentContainer*> status = fast.Next();
    if (status.is_error()) {
      return status.status_value();
    }
    ExtentContainer* current = status.value();

    // Verify the correct iterability of the current node.
    if (fast.Done()) {
      if (inode->extent_count != fast.ExtentIndex() + current->extent_count) {
        FX_LOGS(ERROR) << "Final extent count " << fast.ExtentIndex() + current->extent_count
                       << " does not match inode extent count " << inode->extent_count << " .";
        return ZX_ERR_OUT_OF_RANGE;
      }
    } else if (fast.NextNodeIndex() == slow.NextNodeIndex()) {
      FX_LOGS(ERROR) << "node cycle detected.";
      return ZX_ERR_IO_DATA_INTEGRITY;
    } else if (current->extent_count != kContainerMaxExtents) {
      FX_LOGS(ERROR) << "non-packed extent container found.";
      return ZX_ERR_BAD_STATE;
    }

    // Advance the slow pointer to detct cycles.
    if (++container_count % 2 == 0) {
      zx::result<ExtentContainer*> status = slow.Next();
      if (status.is_error()) {
        return status.status_value();
      }
      if (!fast.Done() && fast.NextNodeIndex() == slow.NextNodeIndex()) {
        FX_LOGS(ERROR) << "Node cycle detected.";
        return ZX_ERR_IO_DATA_INTEGRITY;
      }
    }
  }
  return ZX_OK;
}

bool AllocatedExtentIterator::IsInode() const { return extent_node_ == nullptr; }

const Extent& AllocatedExtentIterator::GetExtent() const {
  if (IsInode()) {
    return inode_->extents[local_index_];
  } else {
    return extent_node_->extents[local_index_];
  }
}

zx_status_t AllocatedExtentIterator::NextContainer() {
  ZX_DEBUG_ASSERT(!node_iterator_.Done());
  uint32_t node_index = node_iterator_.NextNodeIndex();
  // Our implementation uses 0xffffffffu as an end of list indicator to spot attempts to iterate
  // past the end of the list. This value is technically valid but not in any existing practical or
  // debugging use cases.
  ZX_DEBUG_ASSERT(node_index != kMaxNodeId);

  zx::result<ExtentContainer*> status = node_iterator_.Next();
  if (status.is_error()) {
    return status.status_value();
  }
  extent_node_ = status.value();
  local_index_ = 0;
  node_index_ = node_index;

  return ZX_OK;
}

}  // namespace blobfs
