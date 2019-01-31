// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <blobfs/format.h>
#include <blobfs/iterator/allocated-extent-iterator.h>
#include <blobfs/iterator/extent-iterator.h>
#include <zircon/types.h>

namespace blobfs {

AllocatedExtentIterator::AllocatedExtentIterator(NodeFinder* finder, uint32_t node_index)
    : finder_(finder), inode_(finder_->GetNode(node_index)), node_index_(node_index),
      extent_node_(nullptr) {}

bool AllocatedExtentIterator::Done() const {
    return extent_index_ == inode_->extent_count;
}

zx_status_t AllocatedExtentIterator::Next(const Extent** out) {
    ZX_DEBUG_ASSERT(!Done());
    zx_status_t status = ValidateExtentCount();
    if (status != ZX_OK) {
        return status;
    }

    const Extent* extent = GetExtent();
    UpdateIndices(*extent);
    if (!Done() && local_index_ == (IsInode() ? kInlineMaxExtents : extent_node_->extent_count)) {
        zx_status_t status = NextContainer();
        if (status != ZX_OK) {
            return status;
        }
    }

    *out = extent;
    return ZX_OK;
}

uint64_t AllocatedExtentIterator::BlockIndex() const {
    return block_index_;
}

uint32_t AllocatedExtentIterator::ExtentIndex() const {
    return extent_index_;
}

uint32_t AllocatedExtentIterator::NodeIndex() const {
    ZX_DEBUG_ASSERT(!Done());
    return node_index_;
}

bool AllocatedExtentIterator::IsInode() const {
    return extent_node_ == nullptr;
}

zx_status_t AllocatedExtentIterator::ValidateExtentCount() const {
    ZX_DEBUG_ASSERT(local_index_ < (IsInode() ? kInlineMaxExtents : kContainerMaxExtents));
    if (!IsInode() && local_index_ > extent_node_->extent_count) {
        // This container doesn't recognize this extent as valid.
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    return ZX_OK;
}

void AllocatedExtentIterator::UpdateIndices(const Extent& extent) {
    block_index_ += extent.Length();
    local_index_++;
    extent_index_++;
}

const Extent* AllocatedExtentIterator::GetExtent() const {
    if (IsInode()) {
        return &inode_->extents[local_index_];
    } else {
        return &extent_node_->extents[local_index_];
    }
}

uint32_t AllocatedExtentIterator::GetNextNode() const {
    if (IsInode()) {
        return inode_->header.next_node;
    } else {
        return extent_node_->header.next_node;
    }
}

zx_status_t AllocatedExtentIterator::NextContainer() {
    ZX_DEBUG_ASSERT(!Done());
    uint32_t node_index = GetNextNode();

    local_index_ = 0;
    extent_node_ = finder_->GetNode(node_index)->AsExtentContainer();
    node_index_ = node_index;

    ZX_DEBUG_ASSERT(extent_node_ != nullptr);
    bool is_container = extent_node_->header.IsAllocated() &&
                        extent_node_->header.IsExtentContainer();
    if (!is_container) {
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    return ZX_OK;
}

} // namespace blobfs
