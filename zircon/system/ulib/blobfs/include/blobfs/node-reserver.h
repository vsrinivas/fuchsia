// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <bitmap/rle-bitmap.h>
#include <blobfs/format.h>
#include <zircon/types.h>

namespace blobfs {

// Allows nodes to be reserved and unreserved. The purpose of reservation is to allow allocation of
// nodes to occur without yet allocating structures which could be written out to durable storage.
//
// These nodes may be observed by derived classes of NodeReserver.
// Thread-compatible.
class NodeReserver {
public:
    // Reserves space for a node in memory. Does not update disk.
    void Reserve(uint32_t node_index);

    // Unreserves space for a node in memory. Does not update disk.
    void Unreserve(uint32_t node_index);

    // Returns the total number of reserved nodes.
    uint32_t ReservedNodeCount() const;

protected:
    // Returns true if the node at |node_index| is reserved.
    bool IsNodeReserved(uint32_t node_index) const;

    // Informs the NodeReserver that |node_index| has been released.
    //
    // If |node_index| is lower than the lowest known free node, update our
    // assumption of the lowest possible free node.
    void SetFreeNodeLowerBoundIfSmallest(uint32_t node_index);

    // Informs the NodeReserver that |node_index| is the lower bound on free nodes.
    //
    // Should only be invoked when it is known that all nodes from [0, node_index) are
    // free. Does not guarantee |node_index| is free.
    void SetFreeNodeLowerBound(uint32_t node_index);

    // Returns the earliest possible free node.
    uint32_t FreeNodeLowerBound() const;

private:
    // free_node_lower_bound_ is lower bound on free nodes, meaning we are sure that
    // there are no free nodes with indices less than free_node_lower_bound_.
    //
    // By "free", in this context, we mean both "unreserved and unallocated".
    //
    // This doesn't mean that free_node_lower_bound_ is a free node; it just means that one can
    // start looking for a free node from free_node_lower_bound_.
    uint32_t free_node_lower_bound_ = 0;
    bitmap::RleBitmap reserved_nodes_ = {};
};

// Wraps a node reservation in RAII to hold the reservation active, and release it when it goes out
// of scope.
// Thread-compatible.
class ReservedNode {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ReservedNode);

    ReservedNode(NodeReserver* reserver, uint32_t node);
    ReservedNode(ReservedNode&& o);
    ReservedNode& operator=(ReservedNode&& o);

    ~ReservedNode();

    // Access the underlying node index which has been reserved.
    //
    // Unsafe to call if the node has not actually been reserved.
    uint32_t index() const;

    // Releases the underlying node, unreserving it and preventing continued access to |index()|.
    void Reset();

private:
    // Update internal state such that future calls to |Reserved| return false.
    void Release();

    // Identify if the underlying node is reserved, and able to be accessed.
    bool Reserved() const;

    NodeReserver* reserver_;
    uint32_t node_;
};

} // namespace blobfs
