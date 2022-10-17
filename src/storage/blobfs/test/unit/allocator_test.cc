// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/allocator/allocator.h"

#include <zircon/errors.h>

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/mkfs.h"
#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/blobfs/test/blobfs_test_setup.h"
#include "src/storage/blobfs/test/unit/utils.h"

namespace blobfs {
namespace {

using ::id_allocator::IdAllocator;

TEST(AllocatorTest, Null) {
  MockSpaceManager space_manager;
  RawBitmap block_map;
  fzl::ResizeableVmoMapper node_map;
  std::unique_ptr<IdAllocator> nodes_bitmap = {};
  ASSERT_EQ(IdAllocator::Create(0, &nodes_bitmap), ZX_OK);
  Allocator allocator(&space_manager, std::move(block_map), std::move(node_map),
                      std::move(nodes_bitmap));
  allocator.SetLogging(false);

  std::vector<ReservedExtent> extents;
  ASSERT_EQ(ZX_ERR_NO_SPACE, allocator.ReserveBlocks(1, &extents));
  ASSERT_TRUE(allocator.ReserveNode().is_error());
}

TEST(AllocatorTest, Single) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  InitializeAllocator(1, 1, &space_manager, &allocator);

  // We can allocate a single unit.
  std::vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveBlocks(1, &extents), ZX_OK);
  zx::result<ReservedNode> node = allocator->ReserveNode();
  ASSERT_TRUE(node.is_ok());
}

TEST(AllocatorTest, SingleCollision) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  InitializeAllocator(1, 1, &space_manager, &allocator);

  std::vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveBlocks(1, &extents), ZX_OK);
  zx::result<ReservedNode> maybe_node = allocator->ReserveNode();
  ASSERT_TRUE(maybe_node.is_ok());
  ReservedNode& node = maybe_node.value();
  uint32_t node_index = node.index();

  // Check the situation where allocation intersects with the in-flight reservation map.
  std::vector<ReservedExtent> failed_extents;
  ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(1, &failed_extents));
  ASSERT_TRUE(allocator->ReserveNode().is_error());

  // Check the situation where allocation intersects with the committed map.
  allocator->MarkBlocksAllocated(extents[0]);
  allocator->MarkInodeAllocated(std::move(node));
  ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(1, &failed_extents));
  ASSERT_TRUE(allocator->ReserveNode().is_error());

  // Check that freeing the space (and releasing the reservation) makes it
  // available for use once more.
  Extent extent = extents[0].extent();
  allocator->FreeNode(node_index);
  extents.clear();
  allocator->FreeBlocks(extent);
  ASSERT_EQ(allocator->ReserveBlocks(1, &extents), ZX_OK);
  ASSERT_TRUE(allocator->ReserveNode().is_ok());
}

// Test the condition where we cannot allocate because (while looking for
// blocks) we hit an already-allocated prefix of reserved / committed blocks.
TEST(AllocatorTest, PrefixCollision) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  InitializeAllocator(4, 4, &space_manager, &allocator);

  // Allocate a single extent of two blocks.
  std::vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveBlocks(2, &extents), ZX_OK);
  ASSERT_EQ(1ul, extents.size());

  // We have two blocks left; we cannot allocate three blocks.
  std::vector<ReservedExtent> failed_extents;
  ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(3, &failed_extents));
  allocator->MarkBlocksAllocated(extents[0]);
  Extent extent = extents[0].extent();
  extents.clear();

  // After the extents are committed (and unreserved), we still cannot
  // utilize their space.
  ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(3, &failed_extents));

  // After freeing the allocated blocks, we can re-allocate.
  allocator->FreeBlocks(extent);
  ASSERT_EQ(allocator->ReserveBlocks(3, &extents), ZX_OK);
}

// Test the condition where we cannot allocate because (while looking for
// blocks) we hit an already-allocated suffix of reserved / committed blocks.
TEST(AllocatorTest, SuffixCollision) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  InitializeAllocator(4, 4, &space_manager, &allocator);

  // Allocate a single extent of two blocks.
  std::vector<ReservedExtent> prefix_extents;
  ASSERT_EQ(allocator->ReserveBlocks(2, &prefix_extents), ZX_OK);
  ASSERT_EQ(1ul, prefix_extents.size());

  // Allocate another extent of two blocks.
  std::vector<ReservedExtent> suffix_extents;
  ASSERT_EQ(allocator->ReserveBlocks(2, &suffix_extents), ZX_OK);
  ASSERT_EQ(1ul, suffix_extents.size());

  // Release the prefix allocation so we can test against the suffix.
  prefix_extents.clear();

  // We have two blocks left; we cannot allocate three blocks.
  std::vector<ReservedExtent> failed_extents;
  ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(3, &failed_extents));
  allocator->MarkBlocksAllocated(suffix_extents[0]);
  Extent extent = suffix_extents[0].extent();
  suffix_extents.clear();

  // After the extents are committed (and unreserved), we still cannot
  // utilize their space.
  ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(3, &failed_extents));

  // After freeing the allocated blocks, we can re-allocate.
  allocator->FreeBlocks(extent);
  ASSERT_EQ(allocator->ReserveBlocks(3, &suffix_extents), ZX_OK);
}

// Test the condition where our allocation request overlaps with both a
// previously allocated and reserved region.
TEST(AllocatorTest, AllocatedBeforeReserved) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  InitializeAllocator(4, 4, &space_manager, &allocator);

  // Allocate a single extent of one block.
  {
    std::vector<ReservedExtent> prefix_extents;
    ASSERT_EQ(allocator->ReserveBlocks(1, &prefix_extents), ZX_OK);
    ASSERT_EQ(1ul, prefix_extents.size());
    allocator->MarkBlocksAllocated(prefix_extents[0]);
  }

  // Reserve another extent of one block.
  std::vector<ReservedExtent> suffix_extents;
  ASSERT_EQ(allocator->ReserveBlocks(1, &suffix_extents), ZX_OK);
  ASSERT_EQ(1ul, suffix_extents.size());

  // We should still be able to reserve the remaining two blocks in a single
  // extent.
  std::vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveBlocks(2, &extents), ZX_OK);
  ASSERT_EQ(1ul, extents.size());
}

// Test the condition where our allocation request overlaps with both a
// previously allocated and reserved region.
TEST(AllocatorTest, ReservedBeforeAllocated) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  InitializeAllocator(4, 4, &space_manager, &allocator);

  // Reserve an extent of one block.
  std::vector<ReservedExtent> reserved_extents;
  ASSERT_EQ(allocator->ReserveBlocks(1, &reserved_extents), ZX_OK);
  ASSERT_EQ(1ul, reserved_extents.size());

  // Allocate a single extent of one block, immediately following the prior
  // reservation.
  {
    std::vector<ReservedExtent> committed_extents;
    ASSERT_EQ(allocator->ReserveBlocks(1, &committed_extents), ZX_OK);
    ASSERT_EQ(1ul, committed_extents.size());
    allocator->MarkBlocksAllocated(committed_extents[0]);
  }

  // We should still be able to reserve the remaining two blocks in a single
  // extent.
  std::vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveBlocks(2, &extents), ZX_OK);
  ASSERT_EQ(1ul, extents.size());
}

// Tests a case where navigation between multiple reserved and committed blocks
// requires non-trivial logic.
//
// This acts as a regression test against a bug encountered during prototyping,
// where navigating reserved blocks could unintentionally ignore collisions with
// the committed blocks.
TEST(AllocatorTest, InterleavedReservation) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  InitializeAllocator(10, 5, &space_manager, &allocator);

  // R: Reserved
  // C: Committed
  // F: Free
  //
  // [R F F F F F F F F F]
  // Reserve an extent of one block.
  std::vector<ReservedExtent> reservation_group_a;
  ASSERT_EQ(allocator->ReserveBlocks(1, &reservation_group_a), ZX_OK);
  ASSERT_EQ(1ul, reservation_group_a.size());

  // [R R F F F F F F F F]
  // Reserve an extent of one block.
  std::vector<ReservedExtent> reservation_group_b;
  ASSERT_EQ(allocator->ReserveBlocks(1, &reservation_group_b), ZX_OK);
  ASSERT_EQ(1ul, reservation_group_b.size());

  // [R R C F F F F F F F]
  // Allocate a single extent of one block, immediately following the prior
  // reservations.
  {
    std::vector<ReservedExtent> committed_extents;
    ASSERT_EQ(allocator->ReserveBlocks(1, &committed_extents), ZX_OK);
    ASSERT_EQ(1ul, committed_extents.size());
    allocator->MarkBlocksAllocated(committed_extents[0]);
  }

  // [R R C R F F F F F F]
  // Reserve an extent of one block.
  std::vector<ReservedExtent> reservation_group_c;
  ASSERT_EQ(allocator->ReserveBlocks(1, &reservation_group_c), ZX_OK);
  ASSERT_EQ(1ul, reservation_group_c.size());

  // [F R C R F F F F F F]
  // Free the first extent.
  reservation_group_a.clear();

  // We should still be able to reserve the remaining two extents, split
  // across the reservations and the committed block.
  std::vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveBlocks(4, &extents), ZX_OK);
  ASSERT_EQ(2ul, extents.size());
}

TEST(AllocatorTest, IsBlockAllocated) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  InitializeAllocator(4, 4, &space_manager, &allocator);

  // Allocate a single extent of one block.
  std::vector<ReservedExtent> prefix_extents;
  ASSERT_EQ(allocator->ReserveBlocks(1, &prefix_extents), ZX_OK);
  ASSERT_EQ(1ul, prefix_extents.size());
  allocator->MarkBlocksAllocated(prefix_extents[0]);

  auto extent = prefix_extents[0].extent();
  ASSERT_TRUE(allocator->IsBlockAllocated(extent.Start()).value());
  prefix_extents.clear();
  allocator->FreeBlocks(extent);
  ASSERT_FALSE(allocator->IsBlockAllocated(extent.Start()).value());
}

// Create a highly fragmented allocation pool, by allocating every other block,
// and observe that even in the presence of fragmentation we may still acquire
// 100% space utilization.
void RunFragmentationTest(bool keep_even) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  constexpr uint64_t kBlockCount = 16;
  InitializeAllocator(kBlockCount, 4, &space_manager, &allocator);

  // Allocate kBlockCount extents of length one.
  std::vector<ReservedExtent> fragmentation_extents[kBlockCount];
  for (auto& fragmentation_extent : fragmentation_extents) {
    ASSERT_EQ(allocator->ReserveBlocks(1, &fragmentation_extent), ZX_OK);
  }

  // At this point, there shouldn't be a single block of space left.
  std::vector<ReservedExtent> failed_extents;
  ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(1, &failed_extents));

  // Free half of the extents, and demonstrate that we can use all the
  // remaining fragmented space.
  std::vector<ReservedExtent> big_extent;
  static_assert(kBlockCount % 2 == 0, "Test assumes an even-sized allocation pool");
  for (uint64_t i = keep_even ? 1 : 0; i < kBlockCount; i += 2) {
    fragmentation_extents[i].clear();
  }
  ASSERT_EQ(allocator->ReserveBlocks(kBlockCount / 2, &big_extent), ZX_OK);
  big_extent.clear();

  // Commit the reserved extents, and observe that our ability to allocate
  // fragmented extents still persists.
  for (uint64_t i = keep_even ? 0 : 1; i < kBlockCount; i += 2) {
    ASSERT_EQ(1ul, fragmentation_extents[i].size());
    allocator->MarkBlocksAllocated(fragmentation_extents[i][0]);
    fragmentation_extents[i].clear();
  }
  ASSERT_EQ(allocator->ReserveBlocks(kBlockCount / 2, &big_extent), ZX_OK);
  ASSERT_EQ(kBlockCount / 2, big_extent.size());

  // After the big extent is reserved (or committed), however, we cannot reserve
  // anything more.
  ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(1, &failed_extents));
  for (auto& i : big_extent) {
    allocator->MarkBlocksAllocated(i);
  }
  big_extent.clear();
  ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(1, &failed_extents));
}

TEST(AllocatorTest, FragmentationKeepEvenExtents) { RunFragmentationTest(true); }

TEST(AllocatorTest, FragmentationKeepOddExtents) { RunFragmentationTest(false); }

// Test a case of allocation where we try allocating more blocks than can fit
// within a single extent.
TEST(AllocatorTest, MaxExtent) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  constexpr uint64_t kBlockCount = Extent::kBlockCountMax * 2;
  InitializeAllocator(kBlockCount, 4, &space_manager, &allocator);

  // Allocate a region which may be contained within one extent.
  std::vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveBlocks(Extent::kBlockCountMax, &extents), ZX_OK);
  ASSERT_EQ(1ul, extents.size());
  extents.clear();

  // Allocate a region which may not be continued within one extent.
  ASSERT_EQ(allocator->ReserveBlocks(Extent::kBlockCountMax + 1, &extents), ZX_OK);
  ASSERT_EQ(2ul, extents.size());

  // Demonstrate that the remaining blocks are still available.
  std::vector<ReservedExtent> remainder;
  ASSERT_EQ(allocator->ReserveBlocks(kBlockCount - (Extent::kBlockCountMax + 1), &remainder),
            ZX_OK);

  // But nothing more.
  std::vector<ReservedExtent> failed_extent;
  ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(1, &failed_extent));
}

void CheckNodeMapSize(Allocator* allocator, uint64_t size) {
  // Verify that we can allocate |size| nodes...
  std::vector<ReservedNode> nodes;
  ASSERT_EQ(allocator->ReserveNodes(size, &nodes), ZX_OK);

  // ... But no more.
  ASSERT_TRUE(allocator->ReserveNode().is_error());
  ASSERT_EQ(size, allocator->ReservedNodeCount());
}

void CheckBlockMapSize(Allocator* allocator, uint64_t size) {
  // Verify that we can allocate |size| blocks...
  ASSERT_EQ(0ul, allocator->ReservedBlockCount());
  std::vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveBlocks(size, &extents), ZX_OK);

  // ... But no more.
  std::vector<ReservedExtent> failed_extents;
  ASSERT_EQ(ZX_ERR_NO_SPACE, allocator->ReserveBlocks(size, &failed_extents));
}

void ResetSizeHelper(uint64_t before_blocks, uint64_t before_nodes, uint64_t after_blocks,
                     uint64_t after_nodes) {
  // Initialize the allocator with a given size.
  MockTransactionManager transaction_manager;
  RawBitmap block_map;
  ASSERT_EQ(block_map.Reset(before_blocks), ZX_OK);
  fzl::ResizeableVmoMapper node_map;
  size_t map_size = fbl::round_up(before_nodes * kBlobfsInodeSize, kBlobfsBlockSize);
  ASSERT_EQ(node_map.CreateAndMap(map_size, "node map"), ZX_OK);
  transaction_manager.MutableInfo().inode_count = before_nodes;
  transaction_manager.MutableInfo().data_block_count = before_blocks;
  std::unique_ptr<IdAllocator> nodes_bitmap = {};
  ASSERT_EQ(IdAllocator::Create(before_nodes, &nodes_bitmap), ZX_OK);
  Allocator allocator(&transaction_manager, std::move(block_map), std::move(node_map),
                      std::move(nodes_bitmap));
  allocator.SetLogging(false);
  CheckNodeMapSize(&allocator, before_nodes);
  CheckBlockMapSize(&allocator, before_blocks);

  // Update the superblock and reset the sizes.
  transaction_manager.MutableInfo().inode_count = after_nodes;
  transaction_manager.MutableInfo().data_block_count = after_blocks;

  // ResetFromStorage invokes resizing of node and block maps.
  ASSERT_EQ(allocator.ResetFromStorage(transaction_manager), ZX_OK);

  CheckNodeMapSize(&allocator, after_nodes);
  CheckBlockMapSize(&allocator, after_blocks);
}

// Test the functions which can alter the size of the block / node maps after
// initialization.
TEST(AllocatorTest, ResetSize) {
  constexpr uint64_t kNodesPerBlock = kBlobfsBlockSize / kBlobfsInodeSize;

  // Test no changes in size.
  ResetSizeHelper(1, kNodesPerBlock, 1, kNodesPerBlock);
  // Test 2x growth.
  ResetSizeHelper(1, kNodesPerBlock, 2, kNodesPerBlock * 2);
  // Test 8x growth.
  ResetSizeHelper(1, kNodesPerBlock, 8, kNodesPerBlock * 8);
  // Test 2048x growth.
  ResetSizeHelper(1, kNodesPerBlock, 2048, kNodesPerBlock * 2048);

  // Test 2x shrinking.
  ResetSizeHelper(2, kNodesPerBlock * 2, 1, kNodesPerBlock);
  // Test 8x shrinking.
  ResetSizeHelper(8, kNodesPerBlock * 8, 1, kNodesPerBlock);
  // Test 2048x shrinking.
  ResetSizeHelper(2048, kNodesPerBlock * 2048, 1, kNodesPerBlock);
}

void CompareData(const uint8_t* data, const zx::vmo& vmo, size_t bytes) {
  uint64_t vmo_size;
  ASSERT_EQ(vmo.get_size(&vmo_size), ZX_OK);
  ASSERT_GE(vmo_size, bytes);

  uint8_t vmo_buffer[bytes];
  ASSERT_EQ(vmo.read(vmo_buffer, 0, bytes), ZX_OK);
  ASSERT_EQ(0, memcmp(vmo_buffer, data, bytes));
}

void RandomizeData(uint8_t* data, size_t bytes) {
  static unsigned int seed = 0;
  for (size_t i = 0; i < bytes; i++) {
    data[i] = static_cast<uint8_t>(rand_r(&seed));
  }
}

TEST(AllocatorTest, ResetFromStorageTest) {
  MockTransactionManager transaction_manager;

  transaction_manager.MutableInfo().inode_count = 32768;
  transaction_manager.MutableInfo().data_block_count = kBlobfsBlockBits / 2;

  RawBitmap block_map;
  // Keep the block_map aligned to a block multiple
  ASSERT_EQ(block_map.Reset(BlockMapBlocks(transaction_manager.Info()) * kBlobfsBlockBits), ZX_OK);
  ASSERT_EQ(block_map.Shrink(transaction_manager.Info().data_block_count), ZX_OK);

  fzl::ResizeableVmoMapper node_map;
  size_t nodemap_size =
      fbl::round_up(kBlobfsInodeSize * transaction_manager.Info().inode_count, kBlobfsBlockSize);
  ASSERT_EQ(node_map.CreateAndMap(nodemap_size, "nodemap"), ZX_OK);

  std::unique_ptr<IdAllocator> nodes_bitmap = {};
  ASSERT_EQ(IdAllocator::Create(transaction_manager.Info().inode_count, &nodes_bitmap), ZX_OK);

  Allocator allocator(&transaction_manager, std::move(block_map), std::move(node_map),
                      std::move(nodes_bitmap));

  allocator.SetLogging(false);

  uint8_t bitmap_data[kDeviceBlockSize];
  RandomizeData(bitmap_data, kDeviceBlockSize);

  // Set callback which reads |bitmap_data| into each vmo block.
  transaction_manager.SetTransactionCallback(
      [&bitmap_data](const block_fifo_request_t& request, const zx::vmo& vmo) {
        if (request.opcode == BLOCKIO_READ) {
          uint64_t vmo_size;
          zx_status_t status = vmo.get_size(&vmo_size);
          if (status != ZX_OK) {
            return status;
          }

          if (vmo_size < kDeviceBlockSize) {
            return ZX_ERR_BUFFER_TOO_SMALL;
          }

          // |request| may specify a greater length, but for this test its enough to verify that
          // the first |kDeviceBlockSize| bytes were set.
          status = vmo.write(bitmap_data, request.vmo_offset * kBlobfsBlockSize, kDeviceBlockSize);
          if (status != ZX_OK) {
            return status;
          }
        }

        return ZX_OK;
      });

  ASSERT_EQ(allocator.ResetFromStorage(transaction_manager), ZX_OK);

  CompareData(bitmap_data, allocator.GetBlockMapVmo(), kDeviceBlockSize);
  CompareData(bitmap_data, allocator.GetNodeMapVmo(), kDeviceBlockSize);

  // Increase block and inode counts to force maps to resize.
  transaction_manager.MutableInfo().data_block_count *= 2;
  transaction_manager.MutableInfo().inode_count *= 2;

  RandomizeData(bitmap_data, kDeviceBlockSize);
  ASSERT_EQ(allocator.ResetFromStorage(transaction_manager), ZX_OK);

  CompareData(bitmap_data, allocator.GetBlockMapVmo(), kDeviceBlockSize);
  CompareData(bitmap_data, allocator.GetNodeMapVmo(), kDeviceBlockSize);
}

TEST(AllocatorTest, LiveInodePtrBlocksGrow) {
  MockSpaceManager space_manager;
  RawBitmap block_map;
  fzl::ResizeableVmoMapper node_map;
  ASSERT_EQ(node_map.CreateAndMap(kBlobfsBlockSize, "node map"), ZX_OK);
  std::unique_ptr<IdAllocator> nodes_bitmap = {};
  ASSERT_EQ(IdAllocator::Create(0, &nodes_bitmap), ZX_OK);
  Allocator allocator(&space_manager, std::move(block_map), std::move(node_map),
                      std::move(nodes_bitmap));

  // Whilst inode pointer is alive, we cannot grow the node_map.
  auto inode = allocator.GetNode(0);
  ASSERT_TRUE(inode.is_ok());
  bool done = false;
  std::thread thread([&]() {
    ASSERT_EQ(allocator.GrowNodeMap(kBlobfsBlockSize * 5), ZX_OK);
    done = true;
  });
  // Sleeping is usually bad in tests, but this is a halting problem.
  zx::nanosleep(zx::deadline_after(zx::msec(50)));
  EXPECT_FALSE(done);

  // Reset the pointer and the thread should be unblocked.
  inode.value().reset();

  thread.join();
  EXPECT_TRUE(done);
}

TEST(AllocatorTest, TwoInodePtrsDontBlock) {
  MockSpaceManager space_manager;
  RawBitmap block_map;
  fzl::ResizeableVmoMapper node_map;
  ASSERT_EQ(node_map.CreateAndMap(kBlobfsBlockSize, "node map"), ZX_OK);
  std::unique_ptr<IdAllocator> nodes_bitmap = {};
  ASSERT_EQ(IdAllocator::Create(0, &nodes_bitmap), ZX_OK);
  Allocator allocator(&space_manager, std::move(block_map), std::move(node_map),
                      std::move(nodes_bitmap));

  auto inode1 = allocator.GetNode(0);
  ASSERT_TRUE(inode1.is_ok());
  auto inode2 = allocator.GetNode(1);
  ASSERT_TRUE(inode2.is_ok());
}

TEST(AllocatorTest, FreedBlocksAreReservedUntilTransactionCommits) {
  constexpr uint32_t kBlockSize = 512;
  constexpr uint32_t kNumBlocks = 200 * kBlobfsBlockSize / kBlockSize;
  auto device = std::make_unique<block_client::FakeBlockDevice>(kNumBlocks, kBlockSize);

  EXPECT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
  block_client::FakeBlockDevice& device_ref = *device;

  BlobfsTestSetup setup;
  ASSERT_EQ(ZX_OK, setup.Mount(std::move(device)));

  // Create a blob that takes up more than half of the volume.
  fbl::RefPtr<fs::Vnode> root;
  ASSERT_EQ(setup.blobfs()->OpenRootNode(&root), ZX_OK);
  // Use a blob size large enough to fit one, but not two.
  size_t blob_size = setup.blobfs()->Info().data_block_count * kBlobfsBlockSize * 3 / 4;
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(/*mount_path=*/"", blob_size);
  fbl::RefPtr<fs::Vnode> file;
  ASSERT_EQ(root->Create(info->path + 1, 0, &file), ZX_OK);
  size_t actual;
  EXPECT_EQ(file->Truncate(info->size_data), ZX_OK);
  EXPECT_EQ(file->Write(info->data.get(), info->size_data, 0, &actual), ZX_OK);
  EXPECT_EQ(file->Close(), ZX_OK);

  // Attempting to create another blob should result in a no-space condition.
  std::unique_ptr<BlobInfo> info2 = GenerateRandomBlob("", blob_size);
  ASSERT_EQ(root->Create(info2->path + 1, 0, &file), ZX_OK);
  EXPECT_EQ(file->Truncate(info2->size_data), ZX_OK);
  EXPECT_EQ(file->Write(info2->data.get(), info2->size_data, 0, &actual), ZX_ERR_NO_SPACE);
  EXPECT_EQ(file->Close(), ZX_OK);

  // Prevent any more writes from hitting the disk.
  device_ref.Pause();

  // Unlink the blob we just created.
  EXPECT_EQ(root->Unlink(info->path + 1, /*must_be_dir=*/false), ZX_OK);

  std::atomic<bool> done(false);

  std::thread thread([&]() {
    // Creating a new blob should succeed but only after we've unfrozen the device, since it
    // requires syncing the journal and the blocks will remain reserved until that's done.
    zx::nanosleep(zx::deadline_after(zx::msec(10)));
    EXPECT_EQ(done.load(), false);
    device_ref.Resume();
  });

  ASSERT_EQ(root->Create(info2->path + 1, 0, &file), ZX_OK);
  EXPECT_EQ(file->Truncate(info2->size_data), ZX_OK);
  EXPECT_EQ(file->Write(info2->data.get(), info2->size_data, 0, &actual), ZX_OK);
  EXPECT_EQ(file->Close(), ZX_OK);
  done.store(true);

  thread.join();
}

TEST(AllocatorTest, ReservedNodeCountIsCorrect) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  InitializeAllocator(/*blocks=*/1, /*nodes=*/5, &space_manager, &allocator);

  std::vector<ReservedNode> reserved_nodes;
  EXPECT_EQ(allocator->ReserveNodes(/*num_nodes=*/5, &reserved_nodes), ZX_OK);
  EXPECT_EQ(reserved_nodes.size(), 5ul);
  EXPECT_EQ(allocator->ReservedNodeCount(), 5u);

  uint32_t inode_index = reserved_nodes[0].index();
  allocator->MarkInodeAllocated(std::move(reserved_nodes[0]));
  // A reserved node was allocated which makes it no longer reserved.
  EXPECT_EQ(allocator->ReservedNodeCount(), 4u);

  allocator->MarkContainerNodeAllocated(std::move(reserved_nodes[1]), inode_index);
  // Another reserved node was allocated which makes it no longer reserved.
  EXPECT_EQ(allocator->ReservedNodeCount(), 3u);

  // Drop all reserved nodes which will unreserve the remaining 3 nodes.
  reserved_nodes.clear();
  EXPECT_EQ(allocator->ReservedNodeCount(), 0u);
}

TEST(AllocatorTest, MarkInodeAllocatedIsCorrect) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  InitializeAllocator(/*blocks=*/1, /*nodes=*/1, &space_manager, &allocator);

  auto reserved_node = allocator->ReserveNode();
  ASSERT_TRUE(reserved_node.is_ok());
  uint32_t node_index = reserved_node->index();
  allocator->MarkInodeAllocated(std::move(reserved_node).value());
  auto node = allocator->GetNode(node_index);
  ASSERT_NE(node, nullptr);
  EXPECT_TRUE(node->header.IsAllocated());
  EXPECT_TRUE(node->header.IsInode());
}

TEST(AllocatorTest, MarkContainerNodeAllocatedIsCorrect) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  InitializeAllocator(/*blocks=*/1, /*nodes=*/2, &space_manager, &allocator);

  std::vector<ReservedNode> reserved_nodes;
  ASSERT_EQ(allocator->ReserveNodes(2, &reserved_nodes), ZX_OK);
  uint32_t inode_index = reserved_nodes[0].index();
  allocator->MarkInodeAllocated(std::move(reserved_nodes[0]));
  uint32_t node_index = reserved_nodes[1].index();
  allocator->MarkContainerNodeAllocated(std::move(reserved_nodes[1]), inode_index);

  auto node = allocator->GetNode(node_index);
  ASSERT_NE(node, nullptr);
  EXPECT_TRUE(node->header.IsAllocated());
  EXPECT_TRUE(node->header.IsExtentContainer());

  auto inode = allocator->GetNode(inode_index);
  ASSERT_NE(inode, nullptr);
  EXPECT_EQ(inode->header.next_node, node_index);
}

TEST(AllocatorTest, MarkContainerNodeAllocatedWithAnInvalidPreviousNodeIsAnError) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  InitializeAllocator(/*blocks=*/1, /*nodes=*/1, &space_manager, &allocator);

  uint32_t invalid_node_index = kMaxNodeId - 1;
  auto reserved_node = allocator->ReserveNode();
  ASSERT_TRUE(reserved_node.is_ok());
  zx_status_t status =
      allocator->MarkContainerNodeAllocated(std::move(reserved_node).value(), invalid_node_index);
  EXPECT_NE(status, ZX_OK);
}

TEST(AllocatorTest, GetNodeWithAnInvalidIndexReturnsAnError) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  InitializeAllocator(/*blocks=*/1, /*nodes=*/1, &space_manager, &allocator);

  uint32_t invalid_node_index = kMaxNodeId - 1;
  auto inode = allocator->GetNode(invalid_node_index);
  EXPECT_TRUE(inode.is_error());
}

TEST(AllocatorTest, FreeNodeWithAnInvalidIndexReturnsAnError) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  InitializeAllocator(/*blocks=*/1, /*nodes=*/1, &space_manager, &allocator);

  uint32_t invalid_node_index = kMaxNodeId - 1;
  EXPECT_EQ(allocator->FreeNode(invalid_node_index), ZX_ERR_INVALID_ARGS);
}

TEST(AllocatorTest, FreeNodeWithNonAllocatedNodeReturnsAnError) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  InitializeAllocator(/*blocks=*/1, /*nodes=*/5, &space_manager, &allocator);

  EXPECT_EQ(allocator->FreeNode(/*node_index=*/0), ZX_ERR_BAD_STATE);
}

}  // namespace
}  // namespace blobfs
