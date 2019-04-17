// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef KERNEL_LIB_TOPOLOGY_SYSTEM_TOPOLOGY_H_
#define KERNEL_LIB_TOPOLOGY_SYSTEM_TOPOLOGY_H_

#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <zircon/boot/image.h>
#include <zircon/types.h>

/*
 * Captures the physical layout of the core system (processors, caches, etc..).
 * The data will be layed out as a tree, with processor nodes on the bottom and other types above
 * them. The expected usage is to start from a processor node and walk up/down to discover the
 * relationships you are interested in.
 */

namespace system_topology {
// A single node in the topology graph. The union and types here mirror the flat structure,
// zbi_topology_node_t.
struct Node {
    uint8_t entity_type;
    union {
        zbi_topology_processor_t processor;
        zbi_topology_cluster_t cluster;
        zbi_topology_numa_region_t numa_region;
    } entity;
    Node* parent;
    fbl::Vector<Node*> children;
};

// We define a typedef here as we may want to change this type as the design evolves. For example,
// if we add run-time updateability we may want to hold a lock.
typedef const fbl::Vector<Node*>& IterableProcessors;

// A view of the system topology that is defined in early boot and static during the run of the
// system.
class Graph {
public:
    // Takes the flat topology array, validates it, and sets it as the current topology. Returns an
    // error if the topology is invalid.
    //
    // This should only be called during early boot,  after that this data is considered static so
    // no locks are used. If it is desired to set this later in operation than we MUST redesign
    // this process to consider concurrent readers.
    // Returns ZX_ERR_ALREADY_EXISTS if state already set or ZX_ERR_INVALID_ARGS if provided graph
    // fails validation.
    zx_status_t Update(const zbi_topology_node_t* nodes, size_t count);

    // Provides iterable container of pointers to all processor nodes.
    IterableProcessors processors() const {
        return processors_;
    }

    // Number of processor nodes in the topology, this is equivilant to the
    // number of physical processor cores.
    size_t processor_count() const {
        return processors_.size();
    }

    // Number of logical processors in system, this will be different from
    // processor_count() if the system supports SMT.
    size_t logical_processor_count() const {
      return logical_processor_count_;
    }

    // Finds the processor node that is assigned the given logical id.
    // Sets processor to point to that node. If it wasn't found, returns ZX_ERR_NOT_FOUND.
    zx_status_t ProcessorByLogicalId(uint16_t id, Node** processor) const {
        if (id > processors_by_logical_id_.size()) {
            return ZX_ERR_NOT_FOUND;
        }

        *processor = processors_by_logical_id_[id];
        return ZX_OK;
    }

private:
    // Validates that in the provided flat topology:
    //   - all processors are leaf nodes, and all leaf nodes are processors.
    //   - there are no cycles.
    //   - It is stored in a "depth first" ordering, with parents adjacent to
    //   their children.
    bool Validate(const zbi_topology_node_t* nodes, int count) const;

    fbl::unique_ptr<Node[]> nodes_;
    fbl::Vector<Node*> processors_;
    size_t logical_processor_count_ = 0;

    // This is in essence a map with logical ID being the index in the vector.
    // It will contain duplicates for SMT processors so we need it in addition to processors_.
    fbl::Vector<Node*> processors_by_logical_id_;
};

// Get the global instance of the SystemTopology. This will be updated in early boot to contain the
// source of truth view of the system.
// This should be called once before the platform becomes multithreaded. We don't use a raw global
// because we can't ensure that it is initialized, if this is used in the initialization of other
// global objects.
inline Graph& GetMutableSystemTopology() {
    static Graph graph;
    return graph;
}

// The method of the above most things should use, only the platform init code needs the mutable
// version.
inline const Graph& GetSystemTopology() {
    return GetMutableSystemTopology();
}

} // namespace system_topology

#endif //KERNEL_LIB_TOPOLOGY_SYSTEM_TOPOLOGY_H_
