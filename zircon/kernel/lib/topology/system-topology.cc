// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/system-topology.h>
#include <zircon/errors.h>

#include <ktl/move.h>
#include <ktl/unique_ptr.h>

namespace system_topology {

decltype(Graph::system_topology_) Graph::system_topology_;

namespace {

constexpr size_t kMaxTopologyDepth = 20;

inline void ValidationError(size_t index, const char* message) {
  printf("Error validating topology at node %zu : %s\n", index, message);
}

template <typename T>
zx_status_t GrowVector(size_t new_size, fbl::Vector<T>* vector, fbl::AllocChecker* checker) {
  for (size_t i = vector->size(); i < new_size; i++) {
    vector->push_back(T(), checker);
    if (!checker->check()) {
      return ZX_ERR_NO_MEMORY;
    }
  }
  return ZX_OK;
}

}  // namespace

zx_status_t Graph::Initialize(Graph* graph, const zbi_topology_node_t* flat_nodes, size_t count) {
  DEBUG_ASSERT(flat_nodes != nullptr);
  DEBUG_ASSERT(count > 0);

  if (!Validate(flat_nodes, count)) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker checker;
  ktl::unique_ptr<Node[]> nodes(new (&checker) Node[count]{{}});
  if (!checker.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // Create local instances, if successful we will move them to the Graph's fields.
  fbl::Vector<Node*> processors;
  fbl::Vector<Node*> processors_by_logical_id;
  size_t logical_processor_count = 0;

  Node* node = nullptr;
  const zbi_topology_node_t* flat_node = nullptr;
  for (size_t i = 0; i < count; ++i) {
    flat_node = &flat_nodes[i];
    node = &nodes[i];

    node->entity_type = flat_node->entity_type;

    // Copy info.
    switch (node->entity_type) {
      case ZBI_TOPOLOGY_ENTITY_PROCESSOR:
        node->entity.processor = flat_node->entity.processor;

        processors.push_back(node, &checker);
        logical_processor_count += node->entity.processor.logical_id_count;
        if (!checker.check()) {
          return ZX_ERR_NO_MEMORY;
        }

        for (int i = 0; i < node->entity.processor.logical_id_count; ++i) {
          const auto index = node->entity.processor.logical_ids[i];
          GrowVector(index + 1, &processors_by_logical_id, &checker);
          processors_by_logical_id[index] = node;
          if (!checker.check()) {
            return ZX_ERR_NO_MEMORY;
          }
        }
        break;
      case ZBI_TOPOLOGY_ENTITY_CLUSTER:
        node->entity.cluster = flat_node->entity.cluster;
        break;
      case ZBI_TOPOLOGY_ENTITY_NUMA_REGION:
        node->entity.numa_region = flat_node->entity.numa_region;
        break;
      default:
        // Other types don't have attached info.
        break;
    }

    if (flat_node->parent_index != ZBI_TOPOLOGY_NO_PARENT) {
      // Validation should have prevented this.
      ZX_DEBUG_ASSERT_MSG(flat_node->parent_index >= 0 && flat_node->parent_index < count,
                          "parent_index out of range: %u\n", flat_node->parent_index);

      node->parent = &nodes[flat_node->parent_index];
      node->parent->children.push_back(node, &checker);
      if (!checker.check()) {
        return ZX_ERR_NO_MEMORY;
      }
    }
  }

  *graph = Graph{ktl::move(nodes), ktl::move(processors), logical_processor_count,
                 ktl::move(processors_by_logical_id)};
  return ZX_OK;
}

zx_status_t Graph::InitializeSystemTopology(const zbi_topology_node_t* nodes, size_t count) {
  Graph graph;
  const auto status = Graph::Initialize(&graph, nodes, count);
  if (status != ZX_OK) {
    return status;
  }

  // Initialize the global system topology graph instance.
  system_topology_.Initialize(ktl::move(graph));
  return ZX_OK;
}

bool Graph::Validate(const zbi_topology_node_t* nodes, size_t count) {
  DEBUG_ASSERT(nodes != nullptr);
  DEBUG_ASSERT(count > 0);

  uint16_t parents[kMaxTopologyDepth];
  for (size_t i = 0; i < kMaxTopologyDepth; ++i) {
    parents[i] = ZBI_TOPOLOGY_NO_PARENT;
  }

  uint8_t current_type = ZBI_TOPOLOGY_ENTITY_UNDEFINED;
  int current_depth = 0;

  const zbi_topology_node_t* node;
  for (size_t index = 0; index < count; index++) {
    // Traverse the nodes in reverse order.
    const size_t current_index = count - index - 1;

    node = &nodes[current_index];

    if (current_type == ZBI_TOPOLOGY_ENTITY_UNDEFINED) {
      current_type = node->entity_type;
    }

    if (current_type != node->entity_type) {
      if (current_index == parents[current_depth]) {
        // If the type changes then it should be the parent of the
        // previous level.
        current_depth++;

        if (current_depth == kMaxTopologyDepth) {
          ValidationError(current_index, "Structure is too deep, we only support 20 levels.");
          return false;
        }
      } else if (node->entity_type == ZBI_TOPOLOGY_ENTITY_PROCESSOR) {
        // If it isn't the parent of the previous level, but it is a process than we have
        // encountered a new branch and should start walking from the bottom again.

        // Clear the parent index's for all levels but the top, we want to ensure that the
        // top level of the new branch reports to the same parent as we do.
        for (int i = current_depth - 1; i >= 0; --i) {
          parents[i] = ZBI_TOPOLOGY_NO_PARENT;
        }
        current_depth = 0;
      } else {
        // Otherwise the structure is incorrect.
        ValidationError(current_index,
                        "Graph is not stored in correct order, with children adjacent to "
                        "parents");
        return false;
      }
      current_type = node->entity_type;
    }

    if (parents[current_depth] == ZBI_TOPOLOGY_NO_PARENT) {
      parents[current_depth] = node->parent_index;
    } else if (parents[current_depth] != node->parent_index) {
      ValidationError(current_index, "Parents at level do not match.");
      return false;
    }

    // Ensure that all leaf nodes are processors.
    if (current_depth == 0 && node->entity_type != ZBI_TOPOLOGY_ENTITY_PROCESSOR) {
      ValidationError(current_index, "Encountered a leaf node that isn't a processor.");
      return false;
    }

    // Ensure that all processors are leaf nodes.
    if (current_depth != 0 && node->entity_type == ZBI_TOPOLOGY_ENTITY_PROCESSOR) {
      ValidationError(current_index, "Encountered a processor that isn't a leaf node.");
      return false;
    }

    // By the time we reach the first parent we should be at the maximum depth and have no
    // parents defined.
    if (current_index == 0 && parents[current_depth] != ZBI_TOPOLOGY_NO_PARENT &&
        (current_depth == kMaxTopologyDepth - 1 ||
         parents[current_depth + 1] == ZBI_TOPOLOGY_NO_PARENT)) {
      ValidationError(current_index, "Top level of tree should not have a parent");
      return false;
    }
  }
  return true;
}

}  // namespace system_topology
