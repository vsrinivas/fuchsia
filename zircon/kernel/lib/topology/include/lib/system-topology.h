// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_TOPOLOGY_INCLUDE_LIB_SYSTEM_TOPOLOGY_H_
#define ZIRCON_KERNEL_LIB_TOPOLOGY_INCLUDE_LIB_SYSTEM_TOPOLOGY_H_

#include <lib/lazy_init/lazy_init.h>
#include <zircon/boot/image.h>
#include <zircon/types.h>

#include <fbl/vector.h>
#include <kernel/cpu.h>
#include <ktl/move.h>
#include <ktl/unique_ptr.h>

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
  // Initializes the system topology Graph instance from the given flat
  // topology. Performs validation on the flat topology before updating the
  // system graph with the unflattened data. If validation fails an error is
  // returned the system graph is left unmodified in its original state.
  //
  // Note that there is no explicit synchronization protecting concurrent
  // access to the system topology. It is expected to be initialized once at
  // early boot and then remain static and read-only. Relaxing this constraint
  // is possible by adding internal synchronization.
  //
  // Returns ZX_ERR_NO_MEMORY if dynamic memory allocation fails.
  // Returns ZX_ERR_INVALID_ARGS if validation of the flat topology fails.
  static zx_status_t InitializeSystemTopology(const zbi_topology_node_t* nodes, size_t count);

  // Initializes the given topology Graph instance from the given flat
  // topology. Performs validation on the flat topology before updating the
  // |graph| with the unflattened data. If validation fails an error is
  // returned and |graph| is left unmodified in its original state.
  //
  // Returns ZX_ERR_NO_MEMORY if dynamic memory allocation fails.
  // Returns ZX_ERR_INVALID_ARGS if validation of the flat topology fails.
  static zx_status_t Initialize(Graph* graph, const zbi_topology_node_t* nodes, size_t count);

  // Graph instances are default constructible to empty.
  Graph() = default;

  // Constructs a Graph instance from the given unflattened topology data.
  Graph(ktl::unique_ptr<Node[]> nodes, fbl::Vector<Node*> processors,
        size_t logical_processor_count, fbl::Vector<Node*> processors_by_logical_id)
      : nodes_{ktl::move(nodes)},
        processors_{ktl::move(processors)},
        logical_processor_count_{logical_processor_count},
        processors_by_logical_id_{ktl::move(processors_by_logical_id)} {}

  // Graph instances are not copyable.
  Graph(const Graph&) = delete;
  Graph& operator=(const Graph&) = delete;

  // Graph instances are movable.
  Graph(Graph&&) = default;
  Graph& operator=(Graph&&) = default;

  // Provides iterable container of pointers to all processor nodes.
  IterableProcessors processors() const { return processors_; }

  // Number of processor nodes in the topology, this is equivilant to the
  // number of physical processor cores.
  size_t processor_count() const { return processors_.size(); }

  // Number of logical processors in system, this will be different from
  // processor_count() if the system supports SMT.
  size_t logical_processor_count() const { return logical_processor_count_; }

  // Finds the processor node that is assigned the given logical id.
  // Sets processor to point to that node. If it wasn't found, returns ZX_ERR_NOT_FOUND.
  zx_status_t ProcessorByLogicalId(cpu_num_t id, Node** processor) const {
    if (id > processors_by_logical_id_.size()) {
      return ZX_ERR_NOT_FOUND;
    }

    *processor = processors_by_logical_id_[id];
    return ZX_OK;
  }

  // Returns an immutable reference to the system topology graph. This may be
  // called after the graph is initialized by Graph::InitializeSystemTopology.
  static const Graph& GetSystemTopology() { return system_topology_.Get(); }

 private:
  // Validates that in the provided flat topology:
  //   - all processors are leaf nodes, and all leaf nodes are processors.
  //   - there are no cycles.
  //   - It is stored in a "depth first" ordering, with parents adjacent to
  //   their children.
  static bool Validate(const zbi_topology_node_t* nodes, size_t count);

  ktl::unique_ptr<Node[]> nodes_;
  fbl::Vector<Node*> processors_;
  size_t logical_processor_count_{0};

  // This is in essence a map with logical ID being the index in the vector.
  // It will contain duplicates for SMT processors so we need it in addition to processors_.
  fbl::Vector<Node*> processors_by_logical_id_;

  // The graph of the system topology. Initialized once during early boot.
  static lazy_init::LazyInit<Graph, lazy_init::CheckType::Basic> system_topology_;
};

inline const Graph& GetSystemTopology() { return Graph::GetSystemTopology(); }

}  // namespace system_topology

#endif  // ZIRCON_KERNEL_LIB_TOPOLOGY_INCLUDE_LIB_SYSTEM_TOPOLOGY_H_
