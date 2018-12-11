// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/system-topology.h>
#include <unittest/unittest.h>

namespace {

using system_topology::Node;

// Should be larger than the largest topology used here.
constexpr size_t kTopologyArraySize = 60;

struct FlatTopo {
    zbi_topology_node_t nodes[kTopologyArraySize];
    size_t node_count = 0;
};

// Defined at bottom of file, they are long and noisy.

// A generic arm big.LITTLE.
FlatTopo SimpleTopology();

// Roughly a threadripper 2990X.
FlatTopo ComplexTopology();

// Same as Simple but stored with all nodes on a level adjacent to each other.
FlatTopo HierarchicalTopology();

bool test_flat_to_heap_simple() {
    BEGIN_TEST;
    FlatTopo topo = SimpleTopology();

    system_topology::Graph graph;
    ASSERT_EQ(ZX_OK, graph.Update(topo.nodes, topo.node_count));
    ASSERT_EQ(3, graph.processors().size());

    // Test lookup.
    system_topology::Node* node;
    ASSERT_EQ(ZX_OK, graph.ProcessorByLogicalId(1, &node));
    ASSERT_EQ(ZBI_TOPOLOGY_ENTITY_PROCESSOR, node->entity_type);
    ASSERT_EQ(ZBI_TOPOLOGY_PROCESSOR_PRIMARY, node->entity.processor.flags);
    ASSERT_EQ(ZBI_TOPOLOGY_ENTITY_CLUSTER, node->parent->entity_type);
    ASSERT_EQ(1, node->parent->entity.cluster.performance_class);

    END_TEST;
}

bool test_flat_to_heap_complex() {
    BEGIN_TEST;
    FlatTopo topo = ComplexTopology();

    system_topology::Graph graph;
    ASSERT_EQ(ZX_OK, graph.Update(topo.nodes, topo.node_count));
    ASSERT_EQ(32, graph.processors().size());

    END_TEST;
}

bool test_flat_to_heap_walk_result() {
    BEGIN_TEST;
    FlatTopo topo = ComplexTopology();

    system_topology::Graph graph;
    ASSERT_EQ(ZX_OK, graph.Update(topo.nodes, topo.node_count));
    ASSERT_EQ(32, graph.processors().size());

    // For each processor we walk all the way up the graph.
    for (Node* processor : graph.processors()) {
        Node* current = processor;
        Node* next = current->parent;
        while (next != nullptr) {
            // Ensure that the children lists contain all children.
            bool found = false;
            for (Node* child : next->children) {
                found |= child == current;
            }
            ASSERT_TRUE(found,
                        "A node is not listed as a child of its parent.");

            current = current->parent;
            next = current->parent;
        }
    }

    END_TEST;
}

bool test_validate_processor_not_leaf() {
    BEGIN_TEST;
    FlatTopo topo = ComplexTopology();

    // Replace a die node with a processor.
    topo.nodes[1].entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR;

    system_topology::Graph graph;
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, graph.Update(topo.nodes, topo.node_count));

    END_TEST;
}

bool test_validate_leaf_not_processor() {
    BEGIN_TEST;
    FlatTopo topo = SimpleTopology();

    // Replace a processor node with a cluster.
    topo.nodes[4].entity_type = ZBI_TOPOLOGY_ENTITY_CLUSTER;

    system_topology::Graph graph;
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, graph.Update(topo.nodes, topo.node_count));

    END_TEST;
}

bool test_validate_cycle() {
    BEGIN_TEST;
    FlatTopo topo = ComplexTopology();

    // Set the parent index of the die to a processor under it.
    topo.nodes[1].parent_index = 4;

    system_topology::Graph graph;
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, graph.Update(topo.nodes, topo.node_count));

    END_TEST;
}

// This is a cycle like above but fails due to parent mismatch with other nodes
// on its level.
bool test_validate_cycle_shared_parent() {
    BEGIN_TEST;
    FlatTopo topo = ComplexTopology();

    // Set the parent index of the die to a processor under it.
    topo.nodes[2].parent_index = 4;

    system_topology::Graph graph;
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, graph.Update(topo.nodes, topo.node_count));

    END_TEST;
}

// Another logical way to store the graph would be hierarchical, all the top
// level nodes together, followed by the next level, and so on.
// We are proscriptive however that they should be stored in a depth-first
// ordering, so this other ordering should fail validation.
bool test_validate_hierarchical_storage() {
    BEGIN_TEST;
    FlatTopo topo = HierarchicalTopology();

    // Set the parent index of the die to a processor under it.
    topo.nodes[2].parent_index = 4;

    system_topology::Graph graph;
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, graph.Update(topo.nodes, topo.node_count));

    END_TEST;
}

BEGIN_TEST_CASE(system_topology_parsing_tests)
RUN_TEST(test_flat_to_heap_simple)
RUN_TEST(test_flat_to_heap_complex)
RUN_TEST(test_flat_to_heap_walk_result)
END_TEST_CASE(system_topology_parsing_tests)

BEGIN_TEST_CASE(system_topology_validation_tests)
RUN_TEST(test_validate_processor_not_leaf)
RUN_TEST(test_validate_leaf_not_processor)
RUN_TEST(test_validate_cycle)
RUN_TEST(test_validate_cycle_shared_parent)
RUN_TEST(test_validate_hierarchical_storage)
END_TEST_CASE(system_topology_validation_tests)

// Generic ARM big.LITTLE layout.
//   [cluster]       [cluster]
//     [p1]         [p3]   [p4]
FlatTopo SimpleTopology() {
    FlatTopo topo;
    zbi_topology_node_t* nodes = topo.nodes;

    uint16_t logical_processor = 0;
    uint16_t big_cluster = 0, little_cluster = 0;

    size_t index = 0;
    nodes[big_cluster = index++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_CLUSTER,
        .parent_index = ZBI_TOPOLOGY_NO_PARENT,
        .entity = {
            .cluster = {
                .performance_class = 1,
            }}};

    nodes[index++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
        .parent_index = big_cluster,
        .entity = {
            .processor = {
                .logical_ids = {logical_processor++, logical_processor++},
                .logical_id_count = 2,
                .flags = ZBI_TOPOLOGY_PROCESSOR_PRIMARY,
                .architecture = ZBI_TOPOLOGY_ARCH_UNDEFINED,
            }}};

    nodes[little_cluster = index++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_CLUSTER,
        .parent_index = ZBI_TOPOLOGY_NO_PARENT,
        .entity = {
            .cluster = {
                .performance_class = 0,
            }}};

    nodes[index++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
        .parent_index = little_cluster,
        .entity = {
            .processor = {
                .logical_ids = {logical_processor++},
                .logical_id_count = 1,
                .architecture = ZBI_TOPOLOGY_ARCH_UNDEFINED,
            }}};

    nodes[index++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
        .parent_index = little_cluster,
        .entity = {
            .processor = {
                .logical_ids = {logical_processor++},
                .logical_id_count = 1,
                .architecture = ZBI_TOPOLOGY_ARCH_UNDEFINED,
            }}};

    EXPECT_LT(index, kTopologyArraySize);
    topo.node_count = index;
    return topo;
}

FlatTopo HierarchicalTopology() {
    FlatTopo topo;
    zbi_topology_node_t* nodes = topo.nodes;

    uint16_t logical_processor = 0;
    uint16_t big_cluster = 0, little_cluster = 0;

    size_t index = 0;
    nodes[big_cluster = index++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_CLUSTER,
        .parent_index = ZBI_TOPOLOGY_NO_PARENT,
        .entity = {
            .cluster = {
                .performance_class = 1,
            }}};

    nodes[little_cluster = index++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_CLUSTER,
        .parent_index = ZBI_TOPOLOGY_NO_PARENT,
        .entity = {
            .cluster = {
                .performance_class = 0,
            }}};

    nodes[index++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
        .parent_index = big_cluster,
        .entity = {
            .processor = {
                .logical_ids = {logical_processor++, logical_processor++},
                .logical_id_count = 2,
                .flags = ZBI_TOPOLOGY_PROCESSOR_PRIMARY,
                .architecture = ZBI_TOPOLOGY_ARCH_UNDEFINED,
            }}};

    nodes[index++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
        .parent_index = little_cluster,
        .entity = {
            .processor = {
                .logical_ids = {logical_processor++},
                .logical_id_count = 1,
                .flags = ZBI_TOPOLOGY_PROCESSOR_PRIMARY,
                .architecture = ZBI_TOPOLOGY_ARCH_UNDEFINED,
            }}};

    nodes[index++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
        .parent_index = little_cluster,
        .entity = {
            .processor = {
                .logical_ids = {logical_processor++},
                .logical_id_count = 1,
                .flags = ZBI_TOPOLOGY_PROCESSOR_PRIMARY,
                .architecture = ZBI_TOPOLOGY_ARCH_UNDEFINED,
            }}};

    EXPECT_LT(index, kTopologyArraySize);
    topo.node_count = index;
    return topo;
}

// Add a threadripper CCX (CPU complex), a four core cluster.
void AddCCX(uint16_t parent, zbi_topology_node_t* nodes,
            size_t* index, uint16_t* logical_processor) {
    uint16_t cache = 0, cluster = 0;
    nodes[cluster = (*index)++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_CLUSTER,
        .parent_index = parent,
        .entity = {
            .cluster = {
                .performance_class = 0,
            }}};

    nodes[cache = (*index)++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_CACHE,
        .parent_index = cluster,
    };

    for (int i = 0; i < 4; i++) {
        nodes[(*index)++] = (zbi_topology_node_t){
            .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
            .parent_index = cache,
            .entity = {
                .processor = {
                    .logical_ids = {(*logical_processor)++, (*logical_processor)++},
                    .logical_id_count = 2,
                    .architecture = ZBI_TOPOLOGY_ARCH_UNDEFINED,
                }}};
    }
}

// Roughly a threadripper 2990X.
// Four sets of the following:
//                [numa1]
//                [die1]
//     [cluster1]         [cluster2]
//      [cache1]           [cache2]
//  [p1][p2][p3][p4]   [p5][p6][p7][p8]
FlatTopo ComplexTopology() {
    FlatTopo topo;
    zbi_topology_node_t* nodes = topo.nodes;

    uint16_t logical_processor = 0;
    uint16_t die[4] = {0};
    uint16_t numa[4] = {0};

    size_t index = 0;

    nodes[numa[0] = index++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_NUMA_REGION,
        .parent_index = ZBI_TOPOLOGY_NO_PARENT,
        .entity = {
            .numa_region = {
                .start_address = 0x1,
                .end_address = 0x2,
            }}};

    nodes[die[0] = index++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_DIE,
        .parent_index = numa[0],
    };

    AddCCX(die[0], nodes, &index, &logical_processor);
    AddCCX(die[0], nodes, &index, &logical_processor);

    nodes[numa[1] = index++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_NUMA_REGION,
        .parent_index = ZBI_TOPOLOGY_NO_PARENT,
        .entity = {
            .numa_region = {
                .start_address = 0x3,
                .end_address = 0x4,
            }}};

    nodes[die[1] = index++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_DIE,
        .parent_index = numa[1],
    };

    AddCCX(die[1], nodes, &index, &logical_processor);
    AddCCX(die[1], nodes, &index, &logical_processor);

    nodes[numa[2] = index++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_NUMA_REGION,
        .parent_index = ZBI_TOPOLOGY_NO_PARENT,
        .entity = {
            .numa_region = {
                .start_address = 0x5,
                .end_address = 0x6,
            }}};

    nodes[die[2] = index++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_DIE,
        .parent_index = numa[2],
    };

    AddCCX(die[2], nodes, &index, &logical_processor);
    AddCCX(die[2], nodes, &index, &logical_processor);

    nodes[numa[3] = index++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_NUMA_REGION,
        .parent_index = ZBI_TOPOLOGY_NO_PARENT,
        .entity = {
            .numa_region = {
                .start_address = 0x7,
                .end_address = 0x8,
            }}};

    nodes[die[3] = index++] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_DIE,
        .parent_index = numa[3],
    };

    AddCCX(die[3], nodes, &index, &logical_processor);
    AddCCX(die[3], nodes, &index, &logical_processor);

    EXPECT_LT(index, kTopologyArraySize);
    topo.node_count = index;
    return topo;
}

} // namespace

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
