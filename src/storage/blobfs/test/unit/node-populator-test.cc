// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iterator/node-populator.h"

#include <memory>

#include <gtest/gtest.h>

#include "utils.h"

namespace blobfs {
namespace {

TEST(NodePopulatorTest, NodeCount) {
  for (ExtentCountType i = 0; i <= kInlineMaxExtents; i++) {
    EXPECT_EQ(1u, NodePopulator::NodeCountForExtents(i));
  }

  for (ExtentCountType i = kInlineMaxExtents + 1; i <= kInlineMaxExtents + kContainerMaxExtents;
       i++) {
    EXPECT_EQ(2u, NodePopulator::NodeCountForExtents(i));
  }

  for (ExtentCountType i = kInlineMaxExtents + kContainerMaxExtents + 1;
       i <= kInlineMaxExtents + kContainerMaxExtents * 2; i++) {
    EXPECT_EQ(3u, NodePopulator::NodeCountForExtents(i));
  }
}

TEST(NodePopulatorTest, Null) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  InitializeAllocator(1, 1, &space_manager, &allocator);

  fbl::Vector<ReservedExtent> extents;
  fbl::Vector<ReservedNode> nodes;
  ASSERT_EQ(allocator->ReserveNodes(1, &nodes), ZX_OK);
  const uint32_t node_index = nodes[0].index();
  NodePopulator populator(allocator.get(), std::move(extents), std::move(nodes));

  int nodes_visited = 0;
  auto on_node = [&](const ReservedNode& node) {
    ZX_DEBUG_ASSERT(node_index == node.index());
    nodes_visited++;
  };
  auto on_extent = [](ReservedExtent& extent) {
    ZX_DEBUG_ASSERT(false);
    return NodePopulator::IterationCommand::Stop;
  };

  ASSERT_EQ(populator.Walk(on_node, on_extent), ZX_OK);
  ASSERT_EQ(1, nodes_visited);
}

// Test a single node and a single extent.
TEST(NodePopulatorTest, WalkOne) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  InitializeAllocator(1, 1, &space_manager, &allocator);

  fbl::Vector<ReservedNode> nodes;
  ASSERT_EQ(allocator->ReserveNodes(1, &nodes), ZX_OK);
  const uint32_t node_index = nodes[0].index();

  fbl::Vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveBlocks(1, &extents), ZX_OK);
  ASSERT_EQ(1ul, extents.size());
  const Extent allocated_extent = extents[0].extent();

  NodePopulator populator(allocator.get(), std::move(extents), std::move(nodes));

  int nodes_visited = 0;
  auto on_node = [&](const ReservedNode& node) {
    ZX_DEBUG_ASSERT(node_index == node.index());
    nodes_visited++;
  };
  int extents_visited = 0;
  auto on_extent = [&](ReservedExtent& extent) {
    ZX_DEBUG_ASSERT(allocated_extent.Start() == extent.extent().Start());
    ZX_DEBUG_ASSERT(allocated_extent.Length() == extent.extent().Length());
    extents_visited++;
    return NodePopulator::IterationCommand::Continue;
  };

  // Before walking, observe that the node is not allocated.
  const InodePtr inode = allocator->GetNode(node_index);
  ASSERT_FALSE(inode->header.IsAllocated());
  ASSERT_FALSE(inode->header.IsExtentContainer());
  ASSERT_EQ(0ul, inode->blob_size);
  ASSERT_EQ(0u, inode->extent_count);

  ASSERT_EQ(populator.Walk(on_node, on_extent), ZX_OK);
  ASSERT_EQ(1, nodes_visited);
  ASSERT_EQ(1, extents_visited);

  // After walking, observe that the node is allocated.
  ASSERT_TRUE(inode->header.IsAllocated());
  ASSERT_FALSE(inode->header.IsExtentContainer());
  ASSERT_EQ(0ul, inode->blob_size);
  ASSERT_EQ(1u, inode->extent_count);
  ASSERT_EQ(allocated_extent.Start(), inode->extents[0].Start());
  ASSERT_EQ(allocated_extent.Length(), inode->extents[0].Length());
}

// Test all the extents in a single node.
TEST(NodePopulatorTest, WalkAllInlineExtents) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  constexpr size_t kBlockCount = kInlineMaxExtents * 3;
  InitializeAllocator(kBlockCount, 1, &space_manager, &allocator);
  ForceFragmentation(allocator.get(), kBlockCount);

  fbl::Vector<ReservedNode> nodes;
  ASSERT_EQ(allocator->ReserveNodes(1, &nodes), ZX_OK);

  fbl::Vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveBlocks(kInlineMaxExtents, &extents), ZX_OK);
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
  const InodePtr inode = allocator->GetNode(allocated_nodes[0]);
  ASSERT_FALSE(inode->header.IsAllocated());
  ASSERT_FALSE(inode->header.IsExtentContainer());
  ASSERT_EQ(0ul, inode->blob_size);
  ASSERT_EQ(0u, inode->extent_count);

  ASSERT_EQ(populator.Walk(on_node, on_extent), ZX_OK);
  ASSERT_EQ(1u, nodes_visited);
  ASSERT_EQ(kInlineMaxExtents, extents_visited);

  // After walking, observe that the node is allocated.
  ASSERT_TRUE(inode->header.IsAllocated());
  ASSERT_FALSE(inode->header.IsExtentContainer());
  ASSERT_EQ(0ul, inode->blob_size);
  ASSERT_EQ(kInlineMaxExtents, inode->extent_count);
  for (size_t i = 0; i < kInlineMaxExtents; i++) {
    ASSERT_TRUE(allocated_extents[i] == inode->extents[i]);
  }
}

// Test a node which requires an additional extent container.
TEST(NodePopulatorTest, WalkManyNodes) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  constexpr size_t kBlockCount = kInlineMaxExtents * 5;
  constexpr size_t kNodeCount = 2;
  InitializeAllocator(kBlockCount, kNodeCount, &space_manager, &allocator);
  ForceFragmentation(allocator.get(), kBlockCount);

  constexpr size_t kExpectedExtents = kInlineMaxExtents + 1;

  fbl::Vector<ReservedNode> nodes;
  ASSERT_EQ(allocator->ReserveNodes(kNodeCount, &nodes), ZX_OK);

  fbl::Vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveBlocks(kExpectedExtents, &extents), ZX_OK);
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
  InodePtr inode = allocator->GetNode(allocated_nodes[0]);
  ASSERT_FALSE(inode->header.IsAllocated());
  ASSERT_FALSE(inode->header.IsExtentContainer());
  ASSERT_EQ(0ul, inode->blob_size);
  ASSERT_EQ(0u, inode->extent_count);

  ASSERT_EQ(populator.Walk(on_node, on_extent), ZX_OK);
  ASSERT_EQ(kNodeCount, nodes_visited);
  ASSERT_EQ(kExpectedExtents, extents_visited);

  // After walking, observe that the inode is allocated.
  ASSERT_TRUE(inode->header.IsAllocated());
  ASSERT_FALSE(inode->header.IsExtentContainer());
  ASSERT_EQ(allocated_nodes[1], inode->header.next_node);
  ASSERT_EQ(0ul, inode->blob_size);
  ASSERT_EQ(kExpectedExtents, inode->extent_count);
  for (size_t i = 0; i < kInlineMaxExtents; i++) {
    ASSERT_TRUE(allocated_extents[i] == inode->extents[i]);
  }

  // Additionally, observe that a container node is allocated.
  inode = allocator->GetNode(allocated_nodes[1]);
  ASSERT_TRUE(inode->header.IsAllocated());
  ASSERT_TRUE(inode->header.IsExtentContainer());
  const ExtentContainer* container = inode->AsExtentContainer();
  ASSERT_EQ(allocated_nodes[0], container->previous_node);
  ASSERT_EQ(1u, container->extent_count);
  ASSERT_TRUE(allocated_extents[kInlineMaxExtents] == container->extents[0]);
}

// Test a node which requires multiple additional extent containers.
TEST(NodePopulatorTest, WalkManyContainers) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  constexpr size_t kExpectedExtents = kInlineMaxExtents + kContainerMaxExtents + 1;
  constexpr size_t kNodeCount = 3;
  // Block count is large enough to allow for both fragmentation and the
  // allocation of |kExpectedExtents| extents.
  constexpr size_t kBlockCount = 3 * kExpectedExtents;
  InitializeAllocator(kBlockCount, kNodeCount, &space_manager, &allocator);
  ForceFragmentation(allocator.get(), kBlockCount);

  // Allocate the initial nodes and blocks.
  fbl::Vector<ReservedNode> nodes;
  fbl::Vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveNodes(kNodeCount, &nodes), ZX_OK);
  ASSERT_EQ(allocator->ReserveBlocks(kExpectedExtents, &extents), ZX_OK);
  ASSERT_EQ(kExpectedExtents, extents.size());

  // Keep a copy of the nodes and blocks, since we are passing both to the
  // node populator, but want to verify them afterwards.
  fbl::Vector<Extent> allocated_extents;
  fbl::Vector<uint32_t> allocated_nodes;
  CopyExtents(extents, &allocated_extents);
  CopyNodes(nodes, &allocated_nodes);

  // Before walking, observe that the node is not allocated.
  InodePtr inode = allocator->GetNode(allocated_nodes[0]);
  ASSERT_FALSE(inode->header.IsAllocated());
  ASSERT_FALSE(inode->header.IsExtentContainer());
  ASSERT_EQ(0ul, inode->blob_size);
  ASSERT_EQ(0u, inode->extent_count);

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
  ASSERT_EQ(populator.Walk(on_node, on_extent), ZX_OK);

  ASSERT_EQ(kNodeCount, nodes_visited);
  ASSERT_EQ(kExpectedExtents, extents_visited);

  // After walking, observe that the inode is allocated.
  ASSERT_TRUE(inode->header.IsAllocated());
  ASSERT_FALSE(inode->header.IsExtentContainer());
  ASSERT_EQ(allocated_nodes[1], inode->header.next_node);
  ASSERT_EQ(0ul, inode->blob_size);
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
  ASSERT_EQ(allocated_nodes[1], container->previous_node);
  ASSERT_EQ(1, container->extent_count);
  ASSERT_TRUE(allocated_extents[kInlineMaxExtents + kContainerMaxExtents] == container->extents[0]);
}

// Test walking when extra nodes are left unused.
TEST(NodePopulatorTest, WalkExtraNodes) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  constexpr size_t kAllocatedExtents = kInlineMaxExtents;
  constexpr size_t kAllocatedNodes = 3;
  constexpr size_t kUsedExtents = kAllocatedExtents;
  constexpr size_t kUsedNodes = 1;
  // Block count is large enough to allow for both fragmentation and the
  // allocation of |kAllocatedExtents| extents.
  constexpr size_t kBlockCount = 3 * kAllocatedExtents;
  InitializeAllocator(kBlockCount, kAllocatedNodes, &space_manager, &allocator);
  ForceFragmentation(allocator.get(), kBlockCount);

  // Allocate the initial nodes and blocks.
  fbl::Vector<ReservedNode> nodes;
  fbl::Vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveNodes(kAllocatedNodes, &nodes), ZX_OK);
  ASSERT_EQ(allocator->ReserveBlocks(kAllocatedExtents, &extents), ZX_OK);
  ASSERT_EQ(kAllocatedExtents, extents.size());

  // Keep a copy of the nodes and blocks, since we are passing both to the
  // node populator, but want to verify them afterwards.
  fbl::Vector<Extent> allocated_extents;
  fbl::Vector<uint32_t> allocated_nodes;
  CopyExtents(extents, &allocated_extents);
  CopyNodes(nodes, &allocated_nodes);

  // Before walking, observe that the node is not allocated.
  InodePtr inode = allocator->GetNode(allocated_nodes[0]);
  ASSERT_FALSE(inode->header.IsAllocated());
  ASSERT_FALSE(inode->header.IsExtentContainer());
  ASSERT_EQ(0ul, inode->blob_size);
  ASSERT_EQ(0u, inode->extent_count);

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
  ASSERT_EQ(populator.Walk(on_node, on_extent), ZX_OK);

  ASSERT_EQ(kUsedNodes, nodes_visited);
  ASSERT_EQ(kUsedExtents, extents_visited);

  // After walking, observe that the inode is allocated.
  ASSERT_TRUE(inode->header.IsAllocated());
  ASSERT_FALSE(inode->header.IsExtentContainer());
  ASSERT_EQ(0ul, inode->blob_size);
  ASSERT_EQ(kUsedExtents, inode->extent_count);
  for (size_t i = 0; i < kInlineMaxExtents; i++) {
    ASSERT_TRUE(allocated_extents[i] == inode->extents[i]);
  }

  // Observe that the other nodes are not allocated.
  inode = allocator->GetNode(allocated_nodes[1]);
  ASSERT_FALSE(inode->header.IsAllocated());
  inode = allocator->GetNode(allocated_nodes[2]);
  ASSERT_FALSE(inode->header.IsAllocated());
}

// Test walking when extra extents are left unused. This simulates a case where
// less storage is needed to store the blob than originally allocated (for
// example, while compressing a blob).
TEST(NodePopulatorTest, WalkExtraExtents) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  constexpr size_t kAllocatedExtents = kInlineMaxExtents + kContainerMaxExtents + 1;
  constexpr size_t kAllocatedNodes = 3;
  constexpr size_t kUsedExtents = kInlineMaxExtents;
  constexpr size_t kUsedNodes = 1;
  // Block count is large enough to allow for both fragmentation and the
  // allocation of |kAllocatedExtents| extents.
  constexpr size_t kBlockCount = 3 * kAllocatedExtents;
  InitializeAllocator(kBlockCount, kAllocatedNodes, &space_manager, &allocator);
  ForceFragmentation(allocator.get(), kBlockCount);

  // Allocate the initial nodes and blocks.
  fbl::Vector<ReservedNode> nodes;
  fbl::Vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveNodes(kAllocatedNodes, &nodes), ZX_OK);
  ASSERT_EQ(allocator->ReserveBlocks(kAllocatedExtents, &extents), ZX_OK);
  ASSERT_EQ(kAllocatedExtents, extents.size());

  // Keep a copy of the nodes and blocks, since we are passing both to the
  // node populator, but want to verify them afterwards.
  fbl::Vector<Extent> allocated_extents;
  fbl::Vector<uint32_t> allocated_nodes;
  CopyExtents(extents, &allocated_extents);
  CopyNodes(nodes, &allocated_nodes);

  // Before walking, observe that the node is not allocated.
  InodePtr inode = allocator->GetNode(allocated_nodes[0]);
  ASSERT_FALSE(inode->header.IsAllocated());
  ASSERT_FALSE(inode->header.IsExtentContainer());
  ASSERT_EQ(0ul, inode->blob_size);
  ASSERT_EQ(0ul, inode->extent_count);

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
  ASSERT_EQ(populator.Walk(on_node, on_extent), ZX_OK);

  ASSERT_EQ(kUsedNodes, nodes_visited);
  ASSERT_EQ(kUsedExtents, extents_visited);

  // After walking, observe that the inode is allocated.
  ASSERT_TRUE(inode->header.IsAllocated());
  ASSERT_FALSE(inode->header.IsExtentContainer());
  ASSERT_EQ(0ul, inode->blob_size);
  ASSERT_EQ(kUsedExtents, inode->extent_count);
  for (size_t i = 0; i < kInlineMaxExtents; i++) {
    ASSERT_TRUE(allocated_extents[i] == inode->extents[i]);
  }

  // Observe that the other nodes are not allocated.
  inode = allocator->GetNode(allocated_nodes[1]);
  ASSERT_FALSE(inode->header.IsAllocated());
  inode = allocator->GetNode(allocated_nodes[2]);
  ASSERT_FALSE(inode->header.IsAllocated());
}

}  // namespace
}  // namespace blobfs
