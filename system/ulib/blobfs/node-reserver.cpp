// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <stdbool.h>
#include <stdint.h>

#include <bitmap/rle-bitmap.h>
#include <blobfs/format.h>
#include <blobfs/node-reserver.h>
#include <zircon/types.h>

namespace blobfs {

bool NodeReserver::IsNodeReserved(uint32_t node_index) const {
    return reserved_nodes_.Get(node_index, node_index + 1, nullptr);
}

void NodeReserver::Reserve(uint32_t node_index) {
    ZX_DEBUG_ASSERT(!reserved_nodes_.Get(node_index, node_index + 1, nullptr));

    // Mark it as reserved so no one else can allocate it.
    reserved_nodes_.Set(node_index, node_index + 1);
}

void NodeReserver::Unreserve(uint32_t node_index) {
    ZX_DEBUG_ASSERT(reserved_nodes_.Get(node_index, node_index + 1, nullptr));
    zx_status_t status = reserved_nodes_.Clear(node_index, node_index + 1);
    ZX_DEBUG_ASSERT(status == ZX_OK);

    SetFreeNodeLowerBoundIfSmallest(node_index);
}

uint32_t NodeReserver::ReservedNodeCount() const {
    ZX_DEBUG_ASSERT(reserved_nodes_.num_bits() < std::numeric_limits<uint32_t>::max());
    return static_cast<uint32_t>(reserved_nodes_.num_bits());
}

void NodeReserver::SetFreeNodeLowerBoundIfSmallest(uint32_t node_index) {
    if (free_node_lower_bound_ > node_index) {
        SetFreeNodeLowerBound(node_index);
    }
}

void NodeReserver::SetFreeNodeLowerBound(uint32_t node_index) {
    free_node_lower_bound_ = node_index;
}

uint32_t NodeReserver::FreeNodeLowerBound() const {
    return free_node_lower_bound_;
}

ReservedNode::ReservedNode(NodeReserver* reserver, uint32_t node)
    : reserver_(reserver), node_(node) {
    reserver_->Reserve(node);
}

ReservedNode::ReservedNode(ReservedNode&& o) : reserver_(o.reserver_), node_(o.node_) {
    o.Release();
}

ReservedNode& ReservedNode::operator=(ReservedNode&& o) {
    Reset();
    reserver_ = o.reserver_;
    node_ = o.node_;
    o.Release();
    return *this;
}

ReservedNode::~ReservedNode() {
    Reset();
}

uint32_t ReservedNode::index() const {
    ZX_DEBUG_ASSERT_MSG(Reserved(), "Accessing unreserved node");
    return node_;
}

void ReservedNode::Reset() {
    if (Reserved()) {
        reserver_->Unreserve(index());
    }
    Release();
}

void ReservedNode::Release() {
    reserver_ = nullptr;
}

bool ReservedNode::Reserved() const {
    return reserver_ != nullptr;
}

} // namespace blobfs
