// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "orphaned_node.h"

#include <fbl/ref_ptr.h>

#include "logical_buffer_collection.h"
#include "node_properties.h"

namespace sysmem_driver {

// static
OrphanedNode& OrphanedNode::EmplaceInTree(
    fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection,
    NodeProperties* node_properties) {
  auto orphaned_node =
      fbl::AdoptRef(new OrphanedNode(std::move(logical_buffer_collection), node_properties));
  auto orphaned_node_ptr = orphaned_node.get();
  node_properties->SetNode(orphaned_node);
  return *orphaned_node_ptr;
}

bool OrphanedNode::ReadyForAllocation() { return true; }

void OrphanedNode::Fail(zx_status_t epitaph) {
  // nothing to do here
}

void OrphanedNode::OnBuffersAllocated(const AllocationResult& allocation_result) {
  node_properties().SetBuffersLogicallyAllocated();
  // nothing else to do here
}

BufferCollectionToken* OrphanedNode::buffer_collection_token() { return nullptr; }

const BufferCollectionToken* OrphanedNode::buffer_collection_token() const { return nullptr; }

BufferCollection* OrphanedNode::buffer_collection() { return nullptr; }

const BufferCollection* OrphanedNode::buffer_collection() const { return nullptr; }

OrphanedNode* OrphanedNode::orphaned_node() { return this; }

const OrphanedNode* OrphanedNode::orphaned_node() const { return this; }

bool OrphanedNode::is_connected() const { return false; }

OrphanedNode::OrphanedNode(fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection,
                           NodeProperties* node_properties)
    : Node(std::move(logical_buffer_collection), node_properties) {}

}  // namespace sysmem_driver
