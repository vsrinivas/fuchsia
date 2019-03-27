// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/iterator/node-populator.h>
#include <unittest/unittest.h>

#include "utils.h"

namespace blobfs {
namespace {

bool NodeCountTest() {
    BEGIN_TEST;

    for (ExtentCountType i = 0; i <= kInlineMaxExtents; i++) {
        EXPECT_EQ(1, NodePopulator::NodeCountForExtents(i));
    }

    for (ExtentCountType i = kInlineMaxExtents + 1;
         i <= kInlineMaxExtents + kContainerMaxExtents; i++) {
        EXPECT_EQ(2, NodePopulator::NodeCountForExtents(i));
    }

    for (ExtentCountType i = kInlineMaxExtents + kContainerMaxExtents + 1;
         i <= kInlineMaxExtents + kContainerMaxExtents * 2; i++) {
        EXPECT_EQ(3, NodePopulator::NodeCountForExtents(i));
    }

    END_TEST;
}

bool NullTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    ASSERT_TRUE(InitializeAllocator(1, 1, &space_manager, &allocator));

    fbl::Vector<ReservedExtent> extents;
    fbl::Vector<ReservedNode> nodes;
    ASSERT_EQ(ZX_OK, allocator->ReserveNodes(1, &nodes));
    const uint32_t node_index = nodes[0].index();
    NodePopulator populator(allocator.get(), std::move(extents), std::move(nodes));

    size_t nodes_visited = 0;
    auto on_node = [&](const ReservedNode& node) {
        ZX_DEBUG_ASSERT(node_index == node.index());
        nodes_visited++;
    };
    auto on_extent = [](ReservedExtent& extent) {
        ZX_DEBUG_ASSERT(false);
        return NodePopulator::IterationCommand::Stop;
    };

    ASSERT_EQ(ZX_OK, populator.Walk(on_node, on_extent));
    ASSERT_EQ(1, nodes_visited);
    END_TEST;
}

// Test a single node and a single extent.
bool WalkOneTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    ASSERT_TRUE(InitializeAllocator(1, 1, &space_manager, &allocator));

    fbl::Vector<ReservedNode> nodes;
    ASSERT_EQ(ZX_OK, allocator->ReserveNodes(1, &nodes));
    const uint32_t node_index = nodes[0].index();

    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(1, &extents));
    ASSERT_EQ(1, extents.size());
    const Extent allocated_extent = extents[0].extent();

    NodePopulator populator(allocator.get(), std::move(extents), std::move(nodes));

    size_t nodes_visited = 0;
    auto on_node = [&](const ReservedNode& node) {
        ZX_DEBUG_ASSERT(node_index == node.index());
        nodes_visited++;
    };
    size_t extents_visited = 0;
    auto on_extent = [&](ReservedExtent& extent) {
        ZX_DEBUG_ASSERT(allocated_extent.Start() == extent.extent().Start());
        ZX_DEBUG_ASSERT(allocated_extent.Length() == extent.extent().Length());
        extents_visited++;
        return NodePopulator::IterationCommand::Continue;
    };

    // Before walking, observe that the node is not allocated.
    const Inode* inode = allocator->GetNode(node_index);
    ASSERT_FALSE(inode->header.IsAllocated());
    ASSERT_FALSE(inode->header.IsExtentContainer());
    ASSERT_EQ(0, inode->header.next_node);
    ASSERT_EQ(0, inode->blob_size);
    ASSERT_EQ(0, inode->extent_count);

    ASSERT_EQ(ZX_OK, populator.Walk(on_node, on_extent));
    ASSERT_EQ(1, nodes_visited);
    ASSERT_EQ(1, extents_visited);

    // After walking, observe that the node is allocated.
    ASSERT_TRUE(inode->header.IsAllocated());
    ASSERT_FALSE(inode->header.IsExtentContainer());
    ASSERT_EQ(0, inode->header.next_node);
    ASSERT_EQ(0, inode->blob_size);
    ASSERT_EQ(1, inode->extent_count);
    ASSERT_EQ(allocated_extent.Start(), inode->extents[0].Start());
    ASSERT_EQ(allocated_extent.Length(), inode->extents[0].Length());

    END_TEST;
}

// Test all the extents in a single node.
bool WalkAllInlineExtentsTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    constexpr size_t kBlockCount = kInlineMaxExtents * 3;
    ASSERT_TRUE(InitializeAllocator(kBlockCount, 1, &space_manager, &allocator));
    ASSERT_TRUE(ForceFragmentation(allocator.get(), kBlockCount));

    fbl::Vector<ReservedNode> nodes;
    ASSERT_EQ(ZX_OK, allocator->ReserveNodes(1, &nodes));

    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(kInlineMaxExtents, &extents));
    ASSERT_EQ(kInlineMaxExtents, extents.size());

    fbl::Vector<Extent> allocated_extents;
    CopyExtents(extents, &allocated_extents);
    fbl::Vector<uint32_t> allocated_nodes;
    CopyNodes(nodes, &allocated_nodes);

    NodePopulator populator(allocator.get(), std::move(extents), std::move(nodes));

    size_t nodes_visited = 0;
    auto on_node = [&](const ReservedNode& node) {
        ZX_DEBUG_ASSERT(allocated_nodes[nodes_visited] == node.index());
        nodes_visited++;
    };
    size_t extents_visited = 0;
    auto on_extent = [&](ReservedExtent& extent) {
        ZX_DEBUG_ASSERT(allocated_extents[extents_visited] == extent.extent());
        extents_visited++;
        return NodePopulator::IterationCommand::Continue;
    };

    // Before walking, observe that the node is not allocated.
    const Inode* inode = allocator->GetNode(allocated_nodes[0]);
    ASSERT_FALSE(inode->header.IsAllocated());
    ASSERT_FALSE(inode->header.IsExtentContainer());
    ASSERT_EQ(0, inode->header.next_node);
    ASSERT_EQ(0, inode->blob_size);
    ASSERT_EQ(0, inode->extent_count);

    ASSERT_EQ(ZX_OK, populator.Walk(on_node, on_extent));
    ASSERT_EQ(1, nodes_visited);
    ASSERT_EQ(kInlineMaxExtents, extents_visited);

    // After walking, observe that the node is allocated.
    ASSERT_TRUE(inode->header.IsAllocated());
    ASSERT_FALSE(inode->header.IsExtentContainer());
    ASSERT_EQ(0, inode->header.next_node);
    ASSERT_EQ(0, inode->blob_size);
    ASSERT_EQ(kInlineMaxExtents, inode->extent_count);
    for (size_t i = 0; i < kInlineMaxExtents; i++) {
        ASSERT_TRUE(allocated_extents[i] == inode->extents[i]);
    }

    END_TEST;
}

// Test a node which requires an additional extent container.
bool WalkManyNodesTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    constexpr size_t kBlockCount = kInlineMaxExtents * 5;
    constexpr size_t kNodeCount = 2;
    ASSERT_TRUE(InitializeAllocator(kBlockCount, kNodeCount, &space_manager, &allocator));
    ASSERT_TRUE(ForceFragmentation(allocator.get(), kBlockCount));

    constexpr size_t kExpectedExtents = kInlineMaxExtents + 1;

    fbl::Vector<ReservedNode> nodes;
    ASSERT_EQ(ZX_OK, allocator->ReserveNodes(kNodeCount, &nodes));

    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(kExpectedExtents, &extents));
    ASSERT_EQ(kExpectedExtents, extents.size());

    fbl::Vector<Extent> allocated_extents;
    CopyExtents(extents, &allocated_extents);
    fbl::Vector<uint32_t> allocated_nodes;
    CopyNodes(nodes, &allocated_nodes);

    NodePopulator populator(allocator.get(), std::move(extents), std::move(nodes));

    size_t nodes_visited = 0;
    auto on_node = [&](const ReservedNode& node) {
        ZX_DEBUG_ASSERT(allocated_nodes[nodes_visited] == node.index());
        nodes_visited++;
    };
    size_t extents_visited = 0;
    auto on_extent = [&](ReservedExtent& extent) {
        ZX_DEBUG_ASSERT(allocated_extents[extents_visited] == extent.extent());
        extents_visited++;
        return NodePopulator::IterationCommand::Continue;
    };

    // Before walking, observe that the node is not allocated.
    Inode* inode = allocator->GetNode(allocated_nodes[0]);
    ASSERT_FALSE(inode->header.IsAllocated());
    ASSERT_FALSE(inode->header.IsExtentContainer());
    ASSERT_EQ(0, inode->header.next_node);
    ASSERT_EQ(0, inode->blob_size);
    ASSERT_EQ(0, inode->extent_count);

    ASSERT_EQ(ZX_OK, populator.Walk(on_node, on_extent));
    ASSERT_EQ(kNodeCount, nodes_visited);
    ASSERT_EQ(kExpectedExtents, extents_visited);

    // After walking, observe that the inode is allocated.
    ASSERT_TRUE(inode->header.IsAllocated());
    ASSERT_FALSE(inode->header.IsExtentContainer());
    ASSERT_EQ(allocated_nodes[1], inode->header.next_node);
    ASSERT_EQ(0, inode->blob_size);
    ASSERT_EQ(kExpectedExtents, inode->extent_count);
    for (size_t i = 0; i < kInlineMaxExtents; i++) {
        ASSERT_TRUE(allocated_extents[i] == inode->extents[i]);
    }

    // Additionally, observe that a container node is allocated.
    inode = allocator->GetNode(allocated_nodes[1]);
    ASSERT_TRUE(inode->header.IsAllocated());
    ASSERT_TRUE(inode->header.IsExtentContainer());
    const ExtentContainer* container = inode->AsExtentContainer();
    ASSERT_EQ(0, container->header.next_node);
    ASSERT_EQ(allocated_nodes[0], container->previous_node);
    ASSERT_EQ(1, container->extent_count);
    ASSERT_TRUE(allocated_extents[kInlineMaxExtents] == container->extents[0]);

    END_TEST;
}

// Test a node which requires multiple additional extent containers.
bool WalkManyContainersTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    constexpr size_t kExpectedExtents = kInlineMaxExtents + kContainerMaxExtents + 1;
    constexpr size_t kNodeCount = 3;
    // Block count is large enough to allow for both fragmentation and the
    // allocation of |kExpectedExtents| extents.
    constexpr size_t kBlockCount = 3 * kExpectedExtents;
    ASSERT_TRUE(InitializeAllocator(kBlockCount, kNodeCount, &space_manager, &allocator));
    ASSERT_TRUE(ForceFragmentation(allocator.get(), kBlockCount));

    // Allocate the initial nodes and blocks.
    fbl::Vector<ReservedNode> nodes;
    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveNodes(kNodeCount, &nodes));
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(kExpectedExtents, &extents));
    ASSERT_EQ(kExpectedExtents, extents.size());

    // Keep a copy of the nodes and blocks, since we are passing both to the
    // node populator, but want to verify them afterwards.
    fbl::Vector<Extent> allocated_extents;
    fbl::Vector<uint32_t> allocated_nodes;
    CopyExtents(extents, &allocated_extents);
    CopyNodes(nodes, &allocated_nodes);

    // Before walking, observe that the node is not allocated.
    Inode* inode = allocator->GetNode(allocated_nodes[0]);
    ASSERT_FALSE(inode->header.IsAllocated());
    ASSERT_FALSE(inode->header.IsExtentContainer());
    ASSERT_EQ(0, inode->header.next_node);
    ASSERT_EQ(0, inode->blob_size);
    ASSERT_EQ(0, inode->extent_count);

    size_t nodes_visited = 0;
    auto on_node = [&](const ReservedNode& node) {
        ZX_DEBUG_ASSERT(allocated_nodes[nodes_visited] == node.index());
        nodes_visited++;
    };
    size_t extents_visited = 0;
    auto on_extent = [&](ReservedExtent& extent) {
        ZX_DEBUG_ASSERT(allocated_extents[extents_visited] == extent.extent());
        extents_visited++;
        return NodePopulator::IterationCommand::Continue;
    };

    NodePopulator populator(allocator.get(), std::move(extents), std::move(nodes));
    ASSERT_EQ(ZX_OK, populator.Walk(on_node, on_extent));

    ASSERT_EQ(kNodeCount, nodes_visited);
    ASSERT_EQ(kExpectedExtents, extents_visited);

    // After walking, observe that the inode is allocated.
    ASSERT_TRUE(inode->header.IsAllocated());
    ASSERT_FALSE(inode->header.IsExtentContainer());
    ASSERT_EQ(allocated_nodes[1], inode->header.next_node);
    ASSERT_EQ(0, inode->blob_size);
    ASSERT_EQ(kExpectedExtents, inode->extent_count);
    for (size_t i = 0; i < kInlineMaxExtents; i++) {
        ASSERT_TRUE(allocated_extents[i] == inode->extents[i]);
    }

    // Additionally, observe that two container nodes are allocated.
    inode = allocator->GetNode(allocated_nodes[1]);
    ASSERT_TRUE(inode->header.IsAllocated());
    ASSERT_TRUE(inode->header.IsExtentContainer());
    const ExtentContainer* container = inode->AsExtentContainer();
    ASSERT_EQ(allocated_nodes[2], container->header.next_node);
    ASSERT_EQ(allocated_nodes[0], container->previous_node);
    ASSERT_EQ(kContainerMaxExtents, container->extent_count);
    for (size_t i = 0; i < kContainerMaxExtents; i++) {
        ASSERT_TRUE(allocated_extents[kInlineMaxExtents + i] == container->extents[i]);
    }
    inode = allocator->GetNode(allocated_nodes[2]);
    ASSERT_TRUE(inode->header.IsAllocated());
    ASSERT_TRUE(inode->header.IsExtentContainer());
    container = inode->AsExtentContainer();
    ASSERT_EQ(0, container->header.next_node);
    ASSERT_EQ(allocated_nodes[1], container->previous_node);
    ASSERT_EQ(1, container->extent_count);
    ASSERT_TRUE(allocated_extents[kInlineMaxExtents + kContainerMaxExtents] == container->extents[0]);

    END_TEST;
}

// Test walking when extra nodes are left unused.
bool WalkExtraNodesTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    constexpr size_t kAllocatedExtents = kInlineMaxExtents;
    constexpr size_t kAllocatedNodes = 3;
    constexpr size_t kUsedExtents = kAllocatedExtents;
    constexpr size_t kUsedNodes = 1;
    // Block count is large enough to allow for both fragmentation and the
    // allocation of |kAllocatedExtents| extents.
    constexpr size_t kBlockCount = 3 * kAllocatedExtents;
    ASSERT_TRUE(InitializeAllocator(kBlockCount, kAllocatedNodes, &space_manager, &allocator));
    ASSERT_TRUE(ForceFragmentation(allocator.get(), kBlockCount));

    // Allocate the initial nodes and blocks.
    fbl::Vector<ReservedNode> nodes;
    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveNodes(kAllocatedNodes, &nodes));
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(kAllocatedExtents, &extents));
    ASSERT_EQ(kAllocatedExtents, extents.size());

    // Keep a copy of the nodes and blocks, since we are passing both to the
    // node populator, but want to verify them afterwards.
    fbl::Vector<Extent> allocated_extents;
    fbl::Vector<uint32_t> allocated_nodes;
    CopyExtents(extents, &allocated_extents);
    CopyNodes(nodes, &allocated_nodes);

    // Before walking, observe that the node is not allocated.
    Inode* inode = allocator->GetNode(allocated_nodes[0]);
    ASSERT_FALSE(inode->header.IsAllocated());
    ASSERT_FALSE(inode->header.IsExtentContainer());
    ASSERT_EQ(0, inode->header.next_node);
    ASSERT_EQ(0, inode->blob_size);
    ASSERT_EQ(0, inode->extent_count);

    size_t nodes_visited = 0;
    auto on_node = [&](const ReservedNode& node) {
        ZX_DEBUG_ASSERT(allocated_nodes[nodes_visited] == node.index());
        nodes_visited++;
    };
    size_t extents_visited = 0;
    auto on_extent = [&](ReservedExtent& extent) {
        ZX_DEBUG_ASSERT(allocated_extents[extents_visited] == extent.extent());
        extents_visited++;
        return NodePopulator::IterationCommand::Continue;
    };

    NodePopulator populator(allocator.get(), std::move(extents), std::move(nodes));
    ASSERT_EQ(ZX_OK, populator.Walk(on_node, on_extent));

    ASSERT_EQ(kUsedNodes, nodes_visited);
    ASSERT_EQ(kUsedExtents, extents_visited);

    // After walking, observe that the inode is allocated.
    ASSERT_TRUE(inode->header.IsAllocated());
    ASSERT_FALSE(inode->header.IsExtentContainer());
    ASSERT_EQ(0, inode->header.next_node);
    ASSERT_EQ(0, inode->blob_size);
    ASSERT_EQ(kUsedExtents, inode->extent_count);
    for (size_t i = 0; i < kInlineMaxExtents; i++) {
        ASSERT_TRUE(allocated_extents[i] == inode->extents[i]);
    }

    // Observe that the other nodes are not allocated.
    inode = allocator->GetNode(allocated_nodes[1]);
    ASSERT_FALSE(inode->header.IsAllocated());
    inode = allocator->GetNode(allocated_nodes[2]);
    ASSERT_FALSE(inode->header.IsAllocated());
    END_TEST;
}

// Test walking when extra extents are left unused. This simulates a case where
// less storage is needed to store the blob than originally allocated (for
// example, while compressing a blob).
bool WalkExtraExtentsTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    constexpr size_t kAllocatedExtents = kInlineMaxExtents + kContainerMaxExtents + 1;
    constexpr size_t kAllocatedNodes = 3;
    constexpr size_t kUsedExtents = kInlineMaxExtents;
    constexpr size_t kUsedNodes = 1;
    // Block count is large enough to allow for both fragmentation and the
    // allocation of |kAllocatedExtents| extents.
    constexpr size_t kBlockCount = 3 * kAllocatedExtents;
    ASSERT_TRUE(InitializeAllocator(kBlockCount, kAllocatedNodes, &space_manager, &allocator));
    ASSERT_TRUE(ForceFragmentation(allocator.get(), kBlockCount));

    // Allocate the initial nodes and blocks.
    fbl::Vector<ReservedNode> nodes;
    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveNodes(kAllocatedNodes, &nodes));
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(kAllocatedExtents, &extents));
    ASSERT_EQ(kAllocatedExtents, extents.size());

    // Keep a copy of the nodes and blocks, since we are passing both to the
    // node populator, but want to verify them afterwards.
    fbl::Vector<Extent> allocated_extents;
    fbl::Vector<uint32_t> allocated_nodes;
    CopyExtents(extents, &allocated_extents);
    CopyNodes(nodes, &allocated_nodes);

    // Before walking, observe that the node is not allocated.
    Inode* inode = allocator->GetNode(allocated_nodes[0]);
    ASSERT_FALSE(inode->header.IsAllocated());
    ASSERT_FALSE(inode->header.IsExtentContainer());
    ASSERT_EQ(0, inode->header.next_node);
    ASSERT_EQ(0, inode->blob_size);
    ASSERT_EQ(0, inode->extent_count);

    size_t nodes_visited = 0;
    auto on_node = [&](const ReservedNode& node) {
        ZX_DEBUG_ASSERT(allocated_nodes[nodes_visited] == node.index());
        nodes_visited++;
    };
    size_t extents_visited = 0;
    auto on_extent = [&](ReservedExtent& extent) {
        ZX_DEBUG_ASSERT(allocated_extents[extents_visited] == extent.extent());
        extents_visited++;
        if (extents_visited == kUsedExtents) {
            return NodePopulator::IterationCommand::Stop;
        }
        return NodePopulator::IterationCommand::Continue;
    };

    NodePopulator populator(allocator.get(), std::move(extents), std::move(nodes));
    ASSERT_EQ(ZX_OK, populator.Walk(on_node, on_extent));

    ASSERT_EQ(kUsedNodes, nodes_visited);
    ASSERT_EQ(kUsedExtents, extents_visited);

    // After walking, observe that the inode is allocated.
    ASSERT_TRUE(inode->header.IsAllocated());
    ASSERT_FALSE(inode->header.IsExtentContainer());
    ASSERT_EQ(0, inode->header.next_node);
    ASSERT_EQ(0, inode->blob_size);
    ASSERT_EQ(kUsedExtents, inode->extent_count);
    for (size_t i = 0; i < kInlineMaxExtents; i++) {
        ASSERT_TRUE(allocated_extents[i] == inode->extents[i]);
    }

    // Observe that the other nodes are not allocated.
    inode = allocator->GetNode(allocated_nodes[1]);
    ASSERT_FALSE(inode->header.IsAllocated());
    inode = allocator->GetNode(allocated_nodes[2]);
    ASSERT_FALSE(inode->header.IsAllocated());
    END_TEST;
}

} // namespace
} // namespace blobfs

BEGIN_TEST_CASE(blobfsNodePopulatorTests)
RUN_TEST(blobfs::NodeCountTest)
RUN_TEST(blobfs::NullTest)
RUN_TEST(blobfs::WalkOneTest)
RUN_TEST(blobfs::WalkAllInlineExtentsTest)
RUN_TEST(blobfs::WalkManyNodesTest)
RUN_TEST(blobfs::WalkManyContainersTest)
RUN_TEST(blobfs::WalkExtraNodesTest)
RUN_TEST(blobfs::WalkExtraExtentsTest)
END_TEST_CASE(blobfsNodePopulatorTests)
