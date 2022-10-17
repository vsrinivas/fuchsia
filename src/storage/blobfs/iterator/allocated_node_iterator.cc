// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/iterator/allocated_node_iterator.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/status.h>
#include <stdint.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/node_finder.h"

namespace blobfs {

AllocatedNodeIterator::AllocatedNodeIterator(NodeFinder* finder, uint32_t node_index, Inode* inode)
    : finder_(finder), current_node_index_(node_index), inode_(inode) {
  ZX_ASSERT(finder_ && inode_);
}

bool AllocatedNodeIterator::Done() const {
  return extent_index_ + NodeExtentCount() >= inode_->extent_count;
}

zx::result<ExtentContainer*> AllocatedNodeIterator::Next() {
  ZX_DEBUG_ASSERT(!Done());

  const uint32_t next_node_index = NextNodeIndex();
  auto next_node = finder_->GetNode(next_node_index);
  if (next_node.is_error()) {
    FX_LOGS(ERROR) << "GetNode(" << next_node_index << ") failed: " << next_node.status_value();
    if (inode_) {
      FX_LOGS(ERROR) << "Inode: " << *inode_;
    }
    return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
  }
  ExtentContainer* next = next_node->AsExtentContainer();

  ZX_DEBUG_ASSERT(next != nullptr);
  if (!next->header.IsAllocated() || !next->header.IsExtentContainer() ||
      next->previous_node != current_node_index_ || next->extent_count > kContainerMaxExtents) {
    FX_LOGS(ERROR) << "Next node " << next_node_index << " invalid: " << *next;
    return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
  }
  extent_index_ += NodeExtentCount();
  extent_node_ = next;
  current_node_index_ = next_node_index;

  return zx::ok(extent_node_);
}

uint32_t AllocatedNodeIterator::ExtentIndex() const { return extent_index_; }

uint32_t AllocatedNodeIterator::NextNodeIndex() const {
  ZX_DEBUG_ASSERT(!Done());
  return IsInode() ? inode_->header.next_node : extent_node_->header.next_node;
}

uint32_t AllocatedNodeIterator::NodeExtentCount() const {
  if (IsInode()) {
    return inode_->extent_count > 0 ? 1 : 0;
  }
  return extent_node_->extent_count;
}

bool AllocatedNodeIterator::IsInode() const { return extent_node_ == nullptr; }

}  // namespace blobfs
