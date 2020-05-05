// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "node-populator.h"

#include <stdint.h>
#include <zircon/types.h>

#include <blobfs/format.h>
#include <fbl/vector.h>

#include "allocator/allocator.h"
#include "extent-iterator.h"

namespace blobfs {

NodePopulator::NodePopulator(Allocator* allocator, fbl::Vector<ReservedExtent> extents,
                             fbl::Vector<ReservedNode> nodes)
    : allocator_(allocator), extents_(std::move(extents)), nodes_(std::move(nodes)) {
  ZX_DEBUG_ASSERT(extents_.size() <= kMaxBlobExtents);
  ZX_DEBUG_ASSERT(nodes_.size() >=
                  NodeCountForExtents(static_cast<ExtentCountType>(extents_.size())));
}

uint32_t NodePopulator::NodeCountForExtents(ExtentCountType extent_count) {
  bool out_of_line_extents = extent_count > kInlineMaxExtents;
  uint32_t remaining_extents = out_of_line_extents ? extent_count - kInlineMaxExtents : 0;
  return 1 + ((remaining_extents + kContainerMaxExtents - 1) / kContainerMaxExtents);
}

zx_status_t NodePopulator::Walk(OnNodeCallback on_node, OnExtentCallback on_extent) {
  // The first node is not an extent container, and must be treated differently.
  size_t node_count = 0;
  uint32_t node_index = nodes_[node_count].index();

  InodePtr inode = allocator_->GetNode(node_index);
  allocator_->MarkInodeAllocated(nodes_[node_count]);

  ExtentContainer* container = nullptr;
  uint32_t local_index = 0;
  ExtentCountType extent_index = 0;
  for (; extent_index < extents_.size(); extent_index++) {
    bool next_container = false;
    if (extent_index == kInlineMaxExtents) {
      // At capacity for the extents inside the inode; moving to a container.
      ZX_DEBUG_ASSERT_MSG(nodes_.size() > node_count, "Not enough nodes to hold extents");
      inode->header.next_node = nodes_[node_count + 1].index();
      next_container = true;
    } else if (local_index == kContainerMaxExtents) {
      // At capacity for the extents within a container; moving to another container.
      ZX_DEBUG_ASSERT_MSG(nodes_.size() > node_count, "Not enough nodes to hold extents");
      next_container = true;
    }

    if (next_container) {
      // Acquire the next container node, and connect it to the
      // previous node.
      const ReservedNode& node = nodes_[node_count + 1];
      uint32_t next = node.index();
      uint32_t previous = nodes_[node_count].index();
      allocator_->MarkContainerNodeAllocated(node, previous);
      container = allocator_->GetNode(next)->AsExtentContainer();

      node_count++;
      local_index = 0;
    }

    // Copy the extent into the chosen container.
    IterationCommand command = on_extent(extents_[extent_index]);
    if (extent_index < kInlineMaxExtents) {
      inode->extents[local_index] = extents_[extent_index].extent();
    } else {
      container->extents[local_index] = extents_[extent_index].extent();
      container->extent_count++;
    }

    inode->extent_count++;

    if (command == IterationCommand::Stop) {
      break;
    }

    local_index++;
  }

  // Walk over all nodes in order *after* visiting all extents, now that
  // we know how many of them are used.
  for (size_t i = 0; i < node_count + 1; i++) {
    on_node(nodes_[i]);
  }

  return ZX_OK;
}

}  // namespace blobfs
