// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/allocator.h>
#include <unittest/unittest.h>

#include "utils.h"

using id_allocator::IdAllocator;

namespace blobfs {
namespace {

bool NullTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    RawBitmap block_map;
    fzl::ResizeableVmoMapper node_map;
    std::unique_ptr<IdAllocator> nodes_bitmap = {};
    ASSERT_EQ(ZX_OK, IdAllocator::Create(0, &nodes_bitmap), "nodes bitmap");
    Allocator allocator(&space_manager, std::move(block_map), std::move(node_map),
                        std::move(nodes_bitmap));
    allocator.SetLogging(false);

    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_ERR_NO_SPACE, allocator.ReserveBlocks(1, &extents));
    ASSERT_FALSE(allocator.ReserveNode());

    END_TEST;
}

bool SingleTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    ASSERT_TRUE(InitializeAllocator(1, 1, &space_manager, &allocator));

    // We can allocate a single unit.
    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(1, &extents));
    std::optional<ReservedNode> node = allocator->ReserveNode();
    ASSERT_TRUE(node);

    END_TEST;
}

bool SingleCollisionTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    ASSERT_TRUE(InitializeAllocator(1, 1, &space_manager, &allocator));

    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(1, &extents));
    std::optional<ReservedNode> maybe_node = allocator->ReserveNode();
    ASSERT_TRUE(maybe_node);
    ReservedNode& node = *maybe_node;

    // Check the situation where allocation intersects with the in-flight reservation map.
    fbl::Vector<ReservedExtent> failed_extents;
    ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(1, &failed_extents));
    ASSERT_FALSE(allocator->ReserveNode());

    // Check the situation where allocation intersects with the committed map.
    allocator->MarkBlocksAllocated(extents[0]);
    allocator->MarkInodeAllocated(node);
    ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(1, &failed_extents));
    ASSERT_FALSE(allocator->ReserveNode());

    // Check that freeing the space (and releasing the reservation) makes it
    // available for use once more.
    allocator->FreeBlocks(extents[0].extent());
    allocator->FreeNode(node.index());
    node.Reset();
    extents.reset();
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(1, &extents));
    ASSERT_TRUE(allocator->ReserveNode());

    END_TEST;
}

// Test the condition where we cannot allocate because (while looking for
// blocks) we hit an already-allocated prefix of reserved / committed blocks.
bool PrefixCollisionTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    ASSERT_TRUE(InitializeAllocator(4, 4, &space_manager, &allocator));

    // Allocate a single extent of two blocks.
    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(2, &extents));
    ASSERT_EQ(1, extents.size());

    // We have two blocks left; we cannot allocate three blocks.
    fbl::Vector<ReservedExtent> failed_extents;
    ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(3, &failed_extents));
    allocator->MarkBlocksAllocated(extents[0]);
    Extent extent = extents[0].extent();
    extents.reset();

    // After the extents are committed (and unreserved), we still cannot
    // utilize their space.
    ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(3, &failed_extents));

    // After freeing the allocated blocks, we can re-allocate.
    allocator->FreeBlocks(extent);
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(3, &extents));

    END_TEST;
}

// Test the condition where we cannot allocate because (while looking for
// blocks) we hit an already-allocated suffix of reserved / committed blocks.
bool SuffixCollisionTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    ASSERT_TRUE(InitializeAllocator(4, 4, &space_manager, &allocator));

    // Allocate a single extent of two blocks.
    fbl::Vector<ReservedExtent> prefix_extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(2, &prefix_extents));
    ASSERT_EQ(1, prefix_extents.size());

    // Allocate another extent of two blocks.
    fbl::Vector<ReservedExtent> suffix_extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(2, &suffix_extents));
    ASSERT_EQ(1, suffix_extents.size());

    // Release the prefix allocation so we can test against the suffix.
    prefix_extents.reset();

    // We have two blocks left; we cannot allocate three blocks.
    fbl::Vector<ReservedExtent> failed_extents;
    ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(3, &failed_extents));
    allocator->MarkBlocksAllocated(suffix_extents[0]);
    Extent extent = suffix_extents[0].extent();
    suffix_extents.reset();

    // After the extents are committed (and unreserved), we still cannot
    // utilize their space.
    ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(3, &failed_extents));

    // After freeing the allocated blocks, we can re-allocate.
    allocator->FreeBlocks(extent);
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(3, &suffix_extents));
    END_TEST;
}

// Test the condition where our allocation request overlaps with both a
// previously allocated and reserved region.
bool AllocatedBeforeReservedTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    ASSERT_TRUE(InitializeAllocator(4, 4, &space_manager, &allocator));

    // Allocate a single extent of one block.
    {
        fbl::Vector<ReservedExtent> prefix_extents;
        ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(1, &prefix_extents));
        ASSERT_EQ(1, prefix_extents.size());
        allocator->MarkBlocksAllocated(prefix_extents[0]);
    }

    // Reserve another extent of one block.
    fbl::Vector<ReservedExtent> suffix_extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(1, &suffix_extents));
    ASSERT_EQ(1, suffix_extents.size());

    // We should still be able to reserve the remaining two blocks in a single
    // extent.
    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(2, &extents));
    ASSERT_EQ(1, extents.size());

    END_TEST;
}

// Test the condition where our allocation request overlaps with both a
// previously allocated and reserved region.
bool ReservedBeforeAllocatedTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    ASSERT_TRUE(InitializeAllocator(4, 4, &space_manager, &allocator));

    // Reserve an extent of one block.
    fbl::Vector<ReservedExtent> reserved_extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(1, &reserved_extents));
    ASSERT_EQ(1, reserved_extents.size());

    // Allocate a single extent of one block, immediately following the prior
    // reservation.
    {
        fbl::Vector<ReservedExtent> committed_extents;
        ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(1, &committed_extents));
        ASSERT_EQ(1, committed_extents.size());
        allocator->MarkBlocksAllocated(committed_extents[0]);
    }

    // We should still be able to reserve the remaining two blocks in a single
    // extent.
    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(2, &extents));
    ASSERT_EQ(1, extents.size());

    END_TEST;
}

// Tests a case where navigation between multiple reserved and committed blocks
// requires non-trivial logic.
//
// This acts as a regression test against a bug encountered during prototyping,
// where navigating reserved blocks could unintentionally ignore collisions with
// the committed blocks.
bool InterleavedReservationTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    ASSERT_TRUE(InitializeAllocator(10, 5, &space_manager, &allocator));

    // R: Reserved
    // C: Committed
    // F: Free
    //
    // [R F F F F F F F F F]
    // Reserve an extent of one block.
    fbl::Vector<ReservedExtent> reservation_group_a;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(1, &reservation_group_a));
    ASSERT_EQ(1, reservation_group_a.size());

    // [R R F F F F F F F F]
    // Reserve an extent of one block.
    fbl::Vector<ReservedExtent> reservation_group_b;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(1, &reservation_group_b));
    ASSERT_EQ(1, reservation_group_b.size());

    // [R R C F F F F F F F]
    // Allocate a single extent of one block, immediately following the prior
    // reservations.
    {
        fbl::Vector<ReservedExtent> committed_extents;
        ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(1, &committed_extents));
        ASSERT_EQ(1, committed_extents.size());
        allocator->MarkBlocksAllocated(committed_extents[0]);
    }

    // [R R C R F F F F F F]
    // Reserve an extent of one block.
    fbl::Vector<ReservedExtent> reservation_group_c;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(1, &reservation_group_c));
    ASSERT_EQ(1, reservation_group_c.size());

    // [F R C R F F F F F F]
    // Free the first extent.
    reservation_group_a.reset();

    // We should still be able to reserve the remaining two extents, split
    // across the reservations and the committed block.
    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(4, &extents));
    ASSERT_EQ(2, extents.size());

    END_TEST;
}

// Create a highly fragmented allocation pool, by allocating every other block,
// and observe that even in the prescence of fragmentation we may still acquire
// 100% space utilization.
template <bool EvensReserved>
bool FragmentationTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    constexpr uint64_t kBlockCount = 16;
    ASSERT_TRUE(InitializeAllocator(kBlockCount, 4, &space_manager, &allocator));

    // Allocate kBlockCount extents of length one.
    fbl::Vector<ReservedExtent> fragmentation_extents[kBlockCount];
    for (uint64_t i = 0; i < kBlockCount; i++) {
        ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(1, &fragmentation_extents[i]));
    }

    // At this point, there shouldn't be a single block of space left.
    fbl::Vector<ReservedExtent> failed_extents;
    ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(1, &failed_extents));

    // Free half of the extents, and demonstrate that we can use all the
    // remaining fragmented space.
    fbl::Vector<ReservedExtent> big_extent;
    static_assert(kBlockCount % 2 == 0, "Test assumes an even-sized allocation pool");
    for (uint64_t i = EvensReserved ? 1 : 0; i < kBlockCount; i += 2) {
        fragmentation_extents[i].reset();
    }
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(kBlockCount / 2, &big_extent));
    big_extent.reset();

    // Commit the reserved extents, and observe that our ability to allocate
    // fragmented extents still persists.
    for (uint64_t i = EvensReserved ? 0 : 1; i < kBlockCount; i += 2) {
        ASSERT_EQ(1, fragmentation_extents[i].size());
        allocator->MarkBlocksAllocated(fragmentation_extents[i][0]);
        fragmentation_extents[i].reset();
    }
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(kBlockCount / 2, &big_extent));
    ASSERT_EQ(kBlockCount / 2, big_extent.size());

    // After the big extent is reserved (or committed), however, we cannot reserve
    // anything more.
    ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(1, &failed_extents));
    for (uint64_t i = 0; i < big_extent.size(); i++) {
        allocator->MarkBlocksAllocated(big_extent[i]);
    }
    big_extent.reset();
    ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(1, &failed_extents));

    END_TEST;
}

// Test a case of allocation where we try allocating more blocks than can fit
// within a single extent.
bool MaxExtentTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    constexpr uint64_t kBlockCount = kBlockCountMax * 2;
    ASSERT_TRUE(InitializeAllocator(kBlockCount, 4, &space_manager, &allocator));

    // Allocate a region which may be contained within one extent.
    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(kBlockCountMax, &extents));
    ASSERT_EQ(1, extents.size());
    extents.reset();

    // Allocate a region which may not be contined within one extent.
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(kBlockCountMax + 1, &extents));
    ASSERT_EQ(2, extents.size());

    // Demonstrate that the remaining blocks are still available.
    fbl::Vector<ReservedExtent> remainder;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(kBlockCount - (kBlockCountMax + 1), &remainder));

    // But nothing more.
    fbl::Vector<ReservedExtent> failed_extent;
    ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(1, &failed_extent));

    END_TEST;
}

bool CheckNodeMapSize(Allocator* allocator, uint64_t size) {
    BEGIN_HELPER;

    // Verify that we can allocate |size| nodes...
    fbl::Vector<ReservedNode> nodes;
    ASSERT_EQ(ZX_OK, allocator->ReserveNodes(size, &nodes));

    // ... But no more.
    ASSERT_FALSE(allocator->ReserveNode());
    ASSERT_EQ(size, allocator->ReservedNodeCount());

    END_HELPER;
}

bool CheckBlockMapSize(Allocator* allocator, uint64_t size) {
    BEGIN_HELPER;

    // Verify that we can allocate |size| blocks...
    ASSERT_EQ(0, allocator->ReservedBlockCount());
    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(size, &extents));

    // ... But no more.
    fbl::Vector<ReservedExtent> failed_extents;
    ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(size, &failed_extents));

    END_HELPER;
}

bool ResetSizeHelper(uint64_t before_blocks, uint64_t before_nodes,
                     uint64_t after_blocks, uint64_t after_nodes) {
    BEGIN_HELPER;

    // Initialize the allocator with a given size.
    MockSpaceManager space_manager;
    RawBitmap block_map;
    ASSERT_EQ(ZX_OK, block_map.Reset(before_blocks));
    fzl::ResizeableVmoMapper node_map;
    size_t map_size = fbl::round_up(before_nodes * kBlobfsInodeSize, kBlobfsBlockSize);
    ASSERT_EQ(ZX_OK, node_map.CreateAndMap(map_size, "node map"));
    space_manager.MutableInfo().inode_count = before_nodes;
    space_manager.MutableInfo().data_block_count = before_blocks;
    std::unique_ptr<IdAllocator> nodes_bitmap = {};
    ASSERT_EQ(ZX_OK, IdAllocator::Create(before_nodes, &nodes_bitmap), "nodes bitmap");
    Allocator allocator(&space_manager, std::move(block_map), std::move(node_map),
                        std::move(nodes_bitmap));
    allocator.SetLogging(false);
    ASSERT_TRUE(CheckNodeMapSize(&allocator, before_nodes));
    ASSERT_TRUE(CheckBlockMapSize(&allocator, before_blocks));

    // Update the superblock and reset the sizes.
    space_manager.MutableInfo().inode_count = after_nodes;
    space_manager.MutableInfo().data_block_count = after_blocks;
    ASSERT_EQ(ZX_OK, allocator.ResetBlockMapSize());
    ASSERT_EQ(ZX_OK, allocator.ResetNodeMapSize());

    ASSERT_TRUE(CheckNodeMapSize(&allocator, after_nodes));
    ASSERT_TRUE(CheckBlockMapSize(&allocator, after_blocks));

    END_HELPER;
}

// Test the functions which can alter the size of the block / node maps after
// initialization.
bool ResetSizeTest() {
    BEGIN_TEST;

    constexpr uint64_t kNodesPerBlock = kBlobfsBlockSize / kBlobfsInodeSize;

    // Test no changes in size.
    ASSERT_TRUE(ResetSizeHelper(1, kNodesPerBlock, 1, kNodesPerBlock));
    // Test 2x growth.
    ASSERT_TRUE(ResetSizeHelper(1, kNodesPerBlock, 2, kNodesPerBlock * 2));
    // Test 8x growth.
    ASSERT_TRUE(ResetSizeHelper(1, kNodesPerBlock, 8, kNodesPerBlock * 8));
    // Test 2048x growth.
    ASSERT_TRUE(ResetSizeHelper(1, kNodesPerBlock, 2048, kNodesPerBlock * 2048));

    // Test 2x shrinking.
    ASSERT_TRUE(ResetSizeHelper(2, kNodesPerBlock * 2, 1, kNodesPerBlock));
    // Test 8x shrinking.
    ASSERT_TRUE(ResetSizeHelper(8, kNodesPerBlock * 8, 1, kNodesPerBlock));
    // Test 2048x shrinking.
    ASSERT_TRUE(ResetSizeHelper(2048, kNodesPerBlock * 2048, 1, kNodesPerBlock));

    END_TEST;
}

} // namespace
} // namespace blobfs

BEGIN_TEST_CASE(blobfsAllocatorTests)
RUN_TEST(blobfs::NullTest)
RUN_TEST(blobfs::SingleTest)
RUN_TEST(blobfs::SingleCollisionTest)
RUN_TEST(blobfs::PrefixCollisionTest)
RUN_TEST(blobfs::SuffixCollisionTest)
RUN_TEST(blobfs::AllocatedBeforeReservedTest)
RUN_TEST(blobfs::ReservedBeforeAllocatedTest)
RUN_TEST(blobfs::InterleavedReservationTest)
RUN_TEST(blobfs::FragmentationTest</* EvensReserved = */ true>)
RUN_TEST(blobfs::FragmentationTest</* EvensReserved = */ false>)
RUN_TEST(blobfs::MaxExtentTest)
RUN_TEST(blobfs::ResetSizeTest)
END_TEST_CASE(blobfsAllocatorTests)
