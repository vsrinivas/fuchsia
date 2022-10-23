// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/allocator/base_allocator.h"

#include <lib/zx/result.h>
#include <zircon/errors.h>

#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "id_allocator/id_allocator.h"
#include "src/lib/testing/predicates/status.h"
#include "src/storage/blobfs/allocator/extent_reserver.h"
#include "src/storage/blobfs/allocator/node_reserver.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/node_finder.h"

namespace blobfs {
namespace {

using id_allocator::IdAllocator;
using testing::Eq;
using testing::Property;
using testing::SizeIs;

std::unique_ptr<IdAllocator> CreateNodeBitmap(size_t node_count) {
  std::unique_ptr<IdAllocator> node_bitmap;
  ZX_ASSERT(IdAllocator::Create(node_count, &node_bitmap) == ZX_OK);
  return node_bitmap;
}

RawBitmap CreateBlockBitmap(uint64_t block_count) {
  RawBitmap block_bitmap;
  ZX_ASSERT(block_bitmap.Reset(block_count) == ZX_OK);
  return block_bitmap;
}

class AllocatorForTesting : public BaseAllocator {
 public:
  AllocatorForTesting(uint64_t block_count, size_t node_count, bool allow_growing)
      : BaseAllocator(CreateBlockBitmap(block_count), CreateNodeBitmap(node_count)),
        allow_growing_(allow_growing),
        node_map_(node_count) {}

  using BaseAllocator::GetBlockBitmap;
  using BaseAllocator::GetNodeBitmap;

  // blobfs::NodeFinder interface.
  zx::result<InodePtr> GetNode(uint32_t node_index) final {
    if (node_index >= node_map_.size()) {
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }
    return zx::ok(InodePtr(&node_map_[node_index], InodePtrDeleter(nullptr)));
  }

 protected:
  // blobfs::BaseAllocator interface.
  zx::result<> AddBlocks(uint64_t block_count) final {
    if (!allow_growing_) {
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    }
    RawBitmap& block_bitmap = GetBlockBitmap();
    return zx::make_result(block_bitmap.Grow(block_bitmap.size() + block_count));
  }

  zx::result<> AddNodes() final {
    if (!allow_growing_) {
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    }
    size_t new_node_count = node_map_.size() + 1;
    node_map_.resize(new_node_count);
    return zx::make_result(GetNodeBitmap().Grow(new_node_count));
  }

 private:
  bool allow_growing_;
  std::vector<Inode> node_map_;
};

auto IsReservedExtent(uint64_t start_block, uint64_t length_blocks) {
  return Property("extent", &ReservedExtent::extent, Eq(Extent(start_block, length_blocks)));
}

TEST(BaseAllocatorTest, CheckBlocksAllocatedIsCorrect) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/10, /*allow_growing=*/false);

  EXPECT_OK(allocator.GetBlockBitmap().Set(2, 5));

  EXPECT_TRUE(allocator.CheckBlocksAllocated(2, 5));
  EXPECT_FALSE(allocator.CheckBlocksAllocated(1, 3, nullptr));
  uint64_t first_unset = 20;
  EXPECT_FALSE(allocator.CheckBlocksAllocated(3, 7, &first_unset));
  EXPECT_EQ(first_unset, 5ul);
}

TEST(BaseAllocatorTest, IsBlockAllocatedIsCorrect) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/10, /*allow_growing=*/false);

  EXPECT_OK(allocator.GetBlockBitmap().Set(2, 5));

  auto is_allocated = allocator.IsBlockAllocated(1);
  ASSERT_OK(is_allocated.status_value());
  EXPECT_FALSE(*is_allocated);

  is_allocated = allocator.IsBlockAllocated(2);
  ASSERT_OK(is_allocated.status_value());
  EXPECT_TRUE(*is_allocated);
}

TEST(BaseAllocatorTest, ReserveBlocksWithAllBlocksFreeIsCorrect) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/10, /*allow_growing=*/false);

  std::vector<ReservedExtent> extents;
  EXPECT_OK(allocator.ReserveBlocks(10, &extents));
  ASSERT_THAT(extents, SizeIs(1));
  EXPECT_THAT(extents[0], IsReservedExtent(/*start=*/0, /*length=*/10));

  // Blocks were only reserved, not allocated.
  EXPECT_TRUE(allocator.GetBlockBitmap().Scan(0, 10, false));
}

TEST(BaseAllocatorTest, ReserveBlocksWithAllocatedBlocksIsCorrect) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/10, /*allow_growing=*/false);

  EXPECT_OK(allocator.GetBlockBitmap().Set(2, 5));

  std::vector<ReservedExtent> extents;
  EXPECT_OK(allocator.ReserveBlocks(7, &extents));
  ASSERT_THAT(extents, SizeIs(2));
  EXPECT_THAT(extents[0], IsReservedExtent(/*start=*/0, /*length=*/2));
  EXPECT_THAT(extents[1], IsReservedExtent(/*start=*/5, /*length=*/5));
}

TEST(BaseAllocatorTest, ReserveBlocksWithReservedBlocksIsCorrect) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/10, /*allow_growing=*/false);

  std::vector<ReservedExtent> extents1;
  EXPECT_OK(allocator.ReserveBlocks(2, &extents1));
  ASSERT_THAT(extents1, SizeIs(1));
  EXPECT_THAT(extents1[0], IsReservedExtent(/*start=*/0, /*length=*/2));

  std::vector<ReservedExtent> extents2;
  EXPECT_OK(allocator.ReserveBlocks(2, &extents2));
  ASSERT_THAT(extents2, SizeIs(1));
  EXPECT_THAT(extents2[0], IsReservedExtent(/*start=*/2, /*length=*/2));

  extents1.clear();
  std::vector<ReservedExtent> extents3;
  EXPECT_OK(allocator.ReserveBlocks(4, &extents3));
  ASSERT_THAT(extents3, SizeIs(2));
  EXPECT_THAT(extents3[0], IsReservedExtent(/*start=*/0, /*length=*/2));
  EXPECT_THAT(extents3[1], IsReservedExtent(/*start=*/4, /*length=*/2));
}

TEST(BaseAllocatorTest, ReserveBlocksWithTooManyBlocksForOneExtentIsCorrect) {
  constexpr uint64_t block_count = (1 << 16) + 10;
  AllocatorForTesting allocator(block_count, /*node_count=*/10, /*allow_growing=*/false);

  std::vector<ReservedExtent> extents;
  EXPECT_OK(allocator.ReserveBlocks(block_count, &extents));
  ASSERT_THAT(extents, SizeIs(2));
  EXPECT_THAT(extents[0], IsReservedExtent(/*start=*/0, /*length=*/(1 << 16) - 1));
  EXPECT_THAT(extents[1], IsReservedExtent(/*start=*/(1 << 16) - 1, /*length=*/11));
}

TEST(BaseAllocatorTest, ReserveBlocksWithNotEnoughBlocksAndCanNotGrowReturnsAnError) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/10, /*allow_growing=*/false);

  std::vector<ReservedExtent> extents;
  EXPECT_STATUS(allocator.ReserveBlocks(11, &extents), ZX_ERR_NO_SPACE);
}

TEST(BaseAllocatorTest, ReserveBlocksWithNotEnoughBlocksTriesToGrow) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/10, /*allow_growing=*/true);

  std::vector<ReservedExtent> extents;
  EXPECT_OK(allocator.ReserveBlocks(11, &extents));
  ASSERT_THAT(extents, SizeIs(2));
  EXPECT_THAT(extents[0], IsReservedExtent(/*start=*/0, /*length=*/10));
  EXPECT_THAT(extents[1], IsReservedExtent(/*start=*/10, /*length=*/1));
  EXPECT_EQ(allocator.GetBlockBitmap().size(), 11ul);
}

TEST(BaseAllocatorTest, MarkBlocksAllocatedIsCorrect) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/10, /*allow_growing=*/false);

  std::vector<ReservedExtent> extents;
  EXPECT_OK(allocator.ReserveBlocks(2, &extents));
  ASSERT_THAT(extents, SizeIs(1));
  EXPECT_THAT(extents[0], IsReservedExtent(/*start=*/0, /*length=*/2));

  allocator.MarkBlocksAllocated(extents[0]);
  EXPECT_TRUE(allocator.CheckBlocksAllocated(/*start_block=*/0, /*end_block=*/2, nullptr));
}

TEST(BaseAllocatorTest, FreeBlocksIsCorrect) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/10, /*allow_growing=*/false);

  EXPECT_OK(allocator.GetBlockBitmap().Set(2, 5));

  {
    ReservedExtent extent = allocator.FreeBlocks(Extent(/*start=*/2, /*length=*/3));
    EXPECT_TRUE(allocator.GetBlockBitmap().Scan(2, 5, false));

    // The blocks are unallocated but are now reserved so they can't be reused yet.
    std::vector<ReservedExtent> extents;
    EXPECT_STATUS(allocator.ReserveBlocks(10, &extents), ZX_ERR_NO_SPACE);
  }

  // The reservation went out of scope so now the blocks can be reused.
  std::vector<ReservedExtent> extents;
  EXPECT_OK(allocator.ReserveBlocks(10, &extents));
}

TEST(BaseAllocatorTest, ReserveNodesCanReserveNodes) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/10, /*allow_growing=*/false);

  std::vector<ReservedNode> nodes;
  EXPECT_OK(allocator.ReserveNodes(3, &nodes));
  EXPECT_THAT(nodes, SizeIs(3));
  for (size_t i = 0; i < nodes.size(); ++i) {
    EXPECT_EQ(nodes[i].index(), i);
  }
}

TEST(BaseAllocatorTest, ReserveNodesReturnsAnErrorOnNotEnoughNodes) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/3, /*allow_growing=*/false);

  std::vector<ReservedNode> nodes;
  EXPECT_STATUS(allocator.ReserveNodes(5, &nodes), ZX_ERR_NO_SPACE);
  // There are only 3 nodes but 5 were requested and failed. |nodes| should not contain the 3 nodes
  // that could have been reserved.
  EXPECT_TRUE(nodes.empty());
}

TEST(BaseAllocatorTest, ReserveNodeCanReserveANode) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/3, /*allow_growing=*/false);

  auto node = allocator.ReserveNode();
  ASSERT_OK(node.status_value());
  EXPECT_EQ(node->index(), 0u);
}

TEST(BaseAllocatorTest, ReserveNodeDoesNotReserveAllocatedNodes) {
  constexpr size_t kNodeCount = 3;
  AllocatorForTesting allocator(/*block_count=*/10, kNodeCount, /*allow_growing=*/false);

  // Allocate all of the nodes.
  for (size_t i = 0; i < kNodeCount; ++i) {
    auto node = allocator.ReserveNode();
    ASSERT_OK(node.status_value());
    allocator.MarkInodeAllocated(std::move(node).value());
  }

  EXPECT_STATUS(allocator.ReserveNode().status_value(), ZX_ERR_NO_SPACE);
}

TEST(BaseAllocatorTest, ReserveNodeWillAddMoreNodesWhenItHasRunOut) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/3, /*allow_growing=*/true);

  std::vector<ReservedNode> nodes;
  EXPECT_OK(allocator.ReserveNodes(3, &nodes));

  auto node = allocator.ReserveNode();
  ASSERT_OK(node.status_value());
  EXPECT_EQ(node->index(), 3u);
}

TEST(BaseAllocatorTest, UnreserveNodeIsCorrect) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/3, /*allow_growing=*/false);

  auto node = allocator.ReserveNode();
  ASSERT_OK(node.status_value());
  EXPECT_EQ(node->index(), 0u);

  allocator.UnreserveNode(std::move(node).value());

  // The node can be reserved again.
  node = allocator.ReserveNode();
  ASSERT_OK(node.status_value());
  EXPECT_EQ(node->index(), 0u);
}

TEST(BaseAllocatorTest, ReservedNodeCountIsCorrect) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/3, /*allow_growing=*/false);

  EXPECT_EQ(allocator.ReservedNodeCount(), 0u);

  {
    std::vector<ReservedNode> nodes;
    ASSERT_OK(allocator.ReserveNodes(3, &nodes));
    EXPECT_EQ(allocator.ReservedNodeCount(), 3u);

    allocator.MarkInodeAllocated(std::move(nodes[2]));
    EXPECT_EQ(allocator.ReservedNodeCount(), 2u);
  }

  EXPECT_EQ(allocator.ReservedNodeCount(), 0u);
}

TEST(BaseAllocatorTest, MarkInodeAllocatedIsCorrect) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/3, /*allow_growing=*/false);

  auto reserved_node = allocator.ReserveNode();
  ASSERT_OK(reserved_node.status_value());
  EXPECT_EQ(reserved_node->index(), 0u);

  allocator.MarkInodeAllocated(std::move(reserved_node).value());

  auto inode = allocator.GetNode(0);
  ASSERT_OK(inode.status_value());
  EXPECT_TRUE(inode->header.IsAllocated());
  EXPECT_TRUE(inode->header.IsInode());
}

TEST(BaseAllocatorTest, MarkContainerNodeAllocatedIsCorrect) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/3, /*allow_growing=*/false);

  // The node map is initialized with all 0s so to make sure the previous/next nodes are correctly
  // set the 0th node shouldn't be used.
  auto extra_node = allocator.ReserveNode();

  auto reserved_inode = allocator.ReserveNode();
  ASSERT_OK(reserved_inode.status_value());
  EXPECT_EQ(reserved_inode->index(), 1u);

  auto reserved_container = allocator.ReserveNode();
  ASSERT_OK(reserved_container.status_value());
  EXPECT_EQ(reserved_container->index(), 2u);

  allocator.MarkInodeAllocated(std::move(reserved_inode).value());
  EXPECT_OK(allocator.MarkContainerNodeAllocated(std::move(reserved_container).value(), 1));

  auto container = allocator.GetNode(2);
  ASSERT_OK(container.status_value());
  EXPECT_TRUE(container->header.IsAllocated());
  ASSERT_TRUE(container->header.IsExtentContainer());
  EXPECT_EQ(container->AsExtentContainer()->previous_node, 1u);

  auto inode = allocator.GetNode(1);
  ASSERT_OK(inode.status_value());
  EXPECT_EQ(inode->header.next_node, 2u);
}

TEST(BaseAllocatorTest, MarkContainerNodeAllocatedWithAnInvalidPreviousNodeIsAnError) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/3, /*allow_growing=*/false);

  auto node = allocator.ReserveNode();
  ASSERT_OK(node.status_value());

  EXPECT_STATUS(allocator.MarkContainerNodeAllocated(std::move(node).value(), 50),
                ZX_ERR_OUT_OF_RANGE);
}

TEST(BaseAllocatorTest, MarkNodeAllocatedIsCorrect) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/3, /*allow_growing=*/false);

  allocator.MarkNodeAllocated(0);

  auto inode = allocator.GetNode(0);
  ASSERT_OK(inode.status_value());
  // The node map is not updated, only the in-memory structure.
  EXPECT_FALSE(inode->header.IsAllocated());

  // Verify the in-memory structure was updated by reserving a node and seeing that node 0 was
  // skipped.
  auto reserved_node = allocator.ReserveNode();
  ASSERT_OK(reserved_node.status_value());
  EXPECT_EQ(reserved_node->index(), 1u);
}

TEST(BaseAllocatorTest, FreeNodeIsCorrect) {
  AllocatorForTesting allocator(/*block_count=*/10, /*node_count=*/3, /*allow_growing=*/false);

  auto inode = allocator.GetNode(0);
  ASSERT_OK(inode.status_value());
  {
    auto reserved_node = allocator.ReserveNode();
    ASSERT_OK(reserved_node.status_value());
    EXPECT_EQ(reserved_node->index(), 0u);
    allocator.MarkInodeAllocated(std::move(reserved_node).value());
    EXPECT_TRUE(inode->header.IsAllocated());
  }

  allocator.FreeNode(0);
  EXPECT_FALSE(inode->header.IsAllocated());

  // The node can be reserved again.
  auto reserved_node = allocator.ReserveNode();
  ASSERT_OK(reserved_node.status_value());
  EXPECT_EQ(reserved_node->index(), 0u);
}

TEST(BaseAllocatorTest, GetAllocatedRegionsIsCorrect) {
  AllocatorForTesting allocator(/*block_count=*/20, /*node_count=*/3, /*allow_growing=*/false);

  {
    // Allocate all blocks.
    std::vector<ReservedExtent> extents;
    EXPECT_OK(allocator.ReserveBlocks(20, &extents));
    EXPECT_THAT(extents, SizeIs(1));
    allocator.MarkBlocksAllocated(extents[0]);
  }

  // Make 2 holes:
  // 01234567890123456789
  // 11110000111110000111
  allocator.FreeBlocks(Extent(4, 4));
  allocator.FreeBlocks(Extent(13, 4));

  std::vector<BlockRegion> regions = allocator.GetAllocatedRegions();
  EXPECT_THAT(regions, SizeIs(3));
  EXPECT_EQ(regions[0].offset, 0ul);
  EXPECT_EQ(regions[0].length, 4ul);

  EXPECT_EQ(regions[1].offset, 8ul);
  EXPECT_EQ(regions[1].length, 5ul);

  EXPECT_EQ(regions[2].offset, 17ul);
  EXPECT_EQ(regions[2].length, 3ul);
}

}  // namespace
}  // namespace blobfs
