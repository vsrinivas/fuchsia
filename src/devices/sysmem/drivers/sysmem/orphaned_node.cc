// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "orphaned_node.h"

#include <lib/zx/channel.h>

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

void OrphanedNode::OnBuffersAllocated(const AllocationResult& allocation_result) {
  node_properties().SetBuffersLogicallyAllocated();
  // nothing else to do here
}

BufferCollectionToken* OrphanedNode::buffer_collection_token() { return nullptr; }

const BufferCollectionToken* OrphanedNode::buffer_collection_token() const { return nullptr; }

BufferCollection* OrphanedNode::buffer_collection() { return nullptr; }

const BufferCollection* OrphanedNode::buffer_collection() const { return nullptr; }

BufferCollectionTokenGroup* OrphanedNode::buffer_collection_token_group() { return nullptr; }

const BufferCollectionTokenGroup* OrphanedNode::buffer_collection_token_group() const {
  return nullptr;
}

OrphanedNode* OrphanedNode::orphaned_node() { return this; }

const OrphanedNode* OrphanedNode::orphaned_node() const { return this; }

bool OrphanedNode::is_connected_type() const { return false; }

bool OrphanedNode::is_currently_connected() const { return false; }

const char* OrphanedNode::node_type_string() const { return "orphaned"; }

OrphanedNode::OrphanedNode(fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection,
                           NodeProperties* node_properties)
    : Node(std::move(logical_buffer_collection), node_properties, zx::unowned_channel{}) {
  ZX_DEBUG_ASSERT(create_status() == ZX_OK);
}

void OrphanedNode::BindInternal(zx::channel server_end, ErrorHandlerWrapper error_handler_wrapper) {
  ZX_PANIC("OrphanedNode::BindInternal() called");
}

void OrphanedNode::CloseServerBinding(zx_status_t epitaph) {
  // NOP
}

}  // namespace sysmem_driver
