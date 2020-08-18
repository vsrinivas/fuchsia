// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iterator/vector-extent-iterator.h"

#include <memory>

#include <gtest/gtest.h>

#include "iterator/block-iterator.h"
#include "utils.h"

namespace blobfs {
namespace {

// Allocates a blob with the provided number of extents / nodes.
//
// Returns the allocator, the extents, and nodes used.
void TestSetup(size_t blocks, size_t nodes, bool fragmented, MockSpaceManager* space_manager,
               std::unique_ptr<Allocator>* out_allocator) {
  // Block count is large enough to allow for both fragmentation and the
  // allocation of |blocks| extents.
  const size_t block_count = 3 * blocks;
  InitializeAllocator(block_count, nodes, space_manager, out_allocator);
  if (fragmented) {
    ForceFragmentation(out_allocator->get(), block_count);
  }
}

// Iterate over the null blob.
TEST(VectorExtentIteratorTest, Null) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  fbl::Vector<Extent> allocated_extents;
  fbl::Vector<uint32_t> allocated_nodes;
  constexpr size_t kAllocatedExtents = 0;
  constexpr size_t kAllocatedNodes = 1;

  TestSetup(kAllocatedExtents, kAllocatedNodes, /* fragmented=*/true, &space_manager, &allocator);

  fbl::Vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveBlocks(kAllocatedExtents, &extents), ZX_OK);
  ASSERT_EQ(0ul, extents.size());

  VectorExtentIterator iter(extents);

  ASSERT_TRUE(iter.Done());
  ASSERT_EQ(0ul, iter.BlockIndex());
}

// Iterate over a blob with some extents.
TEST(VectorExtentIteratorTest, MultiExtent) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  fbl::Vector<Extent> allocated_extents;
  fbl::Vector<uint32_t> allocated_nodes;
  constexpr size_t kAllocatedExtents = 10;
  constexpr size_t kAllocatedNodes = 1;

  TestSetup(kAllocatedExtents, kAllocatedNodes, /* fragmented=*/true, &space_manager, &allocator);

  fbl::Vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveBlocks(kAllocatedExtents, &extents), ZX_OK);
  ASSERT_EQ(kAllocatedExtents, extents.size());

  VectorExtentIterator iter(extents);

  size_t blocks_seen = 0;
  for (size_t i = 0; i < kAllocatedExtents; i++) {
    ASSERT_FALSE(iter.Done());

    const Extent* extent;
    ASSERT_EQ(iter.Next(&extent), ZX_OK);
    ASSERT_TRUE(extents[i].extent() == *extent);
    blocks_seen += extent->Length();
    ASSERT_EQ(blocks_seen, iter.BlockIndex());
  }

  ASSERT_TRUE(iter.Done());
}

// Test the usage of the BlockIterator over the vector extent iterator.
TEST(VectorExtentIteratorTest, BlockIterator) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  constexpr size_t kAllocatedExtents = 10;
  constexpr size_t kAllocatedNodes = 1;

  TestSetup(kAllocatedExtents, kAllocatedNodes, /* fragmented=*/true, &space_manager, &allocator);

  fbl::Vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveBlocks(kAllocatedExtents, &extents), ZX_OK);
  ASSERT_EQ(kAllocatedExtents, extents.size());

  BlockIterator iter(std::make_unique<VectorExtentIterator>(extents));
  ASSERT_EQ(0ul, iter.BlockIndex());
  ASSERT_FALSE(iter.Done());

  uint32_t blocks_seen = 0;
  for (size_t i = 0; i < extents.size(); i++) {
    ASSERT_FALSE(iter.Done());
    uint32_t actual_length;
    uint64_t actual_start;
    ASSERT_EQ(iter.Next(1, &actual_length, &actual_start), ZX_OK);
    ASSERT_EQ(1ul, actual_length);
    ASSERT_EQ(extents[i].extent().Start(), actual_start);
    blocks_seen += actual_length;
    ASSERT_EQ(blocks_seen, iter.BlockIndex());
  }

  ASSERT_TRUE(iter.Done());
}

// Test that |IterateToBlock| correctly iterates to the desired block.
TEST(VectorExtentIteratorTest, BlockIteratorRandomStart) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  constexpr size_t kAllocatedExtents = 10;
  constexpr size_t kAllocatedNodes = 1;

  TestSetup(kAllocatedExtents, kAllocatedNodes, /* fragmented=*/true, &space_manager, &allocator);

  fbl::Vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveBlocks(kAllocatedExtents, &extents), ZX_OK);
  ASSERT_EQ(kAllocatedExtents, extents.size());

  for (int i = 0; i < 20; i++) {
    BlockIterator iter(std::make_unique<VectorExtentIterator>(extents));

    uint32_t block_index = static_cast<uint32_t>(rand() % kAllocatedExtents);
    ASSERT_EQ(IterateToBlock(&iter, block_index), ZX_OK);
    ASSERT_EQ(block_index, iter.BlockIndex());
  }

  BlockIterator iter(std::make_unique<VectorExtentIterator>(extents));
  ASSERT_EQ(IterateToBlock(&iter, kAllocatedExtents + 10), ZX_ERR_INVALID_ARGS);
}

void ValidateStreamBlocks(fbl::Vector<ReservedExtent> extents, uint32_t block_count) {
  BlockIterator iter(std::make_unique<VectorExtentIterator>(extents));

  size_t stream_blocks = 0;
  size_t stream_index = 0;
  auto stream_callback = [&](uint64_t local_offset, uint64_t dev_offset, uint32_t length) {
    ZX_DEBUG_ASSERT(stream_blocks == local_offset);
    ZX_DEBUG_ASSERT(extents[stream_index].extent().Start() == dev_offset);
    ZX_DEBUG_ASSERT(extents[stream_index].extent().Length() == length);

    stream_blocks += length;
    stream_index++;
    return ZX_OK;
  };

  ASSERT_EQ(StreamBlocks(&iter, block_count, stream_callback), ZX_OK);
  ASSERT_TRUE(iter.Done());
}

// Test streaming blocks from a fragmented iterator.
TEST(VectorExtentIteratorTest, StreamBlocksFragmented) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  constexpr size_t kAllocatedExtents = 10;
  constexpr size_t kAllocatedBlocks = kAllocatedExtents;
  constexpr size_t kAllocatedNodes = 1;

  TestSetup(kAllocatedBlocks, kAllocatedNodes, /* fragmented=*/true, &space_manager, &allocator);

  fbl::Vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveBlocks(kAllocatedBlocks, &extents), ZX_OK);
  ASSERT_EQ(kAllocatedExtents, extents.size());

  ValidateStreamBlocks(std::move(extents), kAllocatedBlocks);
}

// Test streaming blocks from a contiguous iterator.
TEST(VectorExtentIteratorTest, StreamBlocksContiguous) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  constexpr size_t kAllocatedExtents = 1;
  constexpr size_t kAllocatedBlocks = 10;
  constexpr size_t kAllocatedNodes = 1;

  TestSetup(kAllocatedBlocks, kAllocatedNodes, /* fragmented=*/false, &space_manager, &allocator);

  fbl::Vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveBlocks(kAllocatedBlocks, &extents), ZX_OK);
  ASSERT_EQ(kAllocatedExtents, extents.size());

  ValidateStreamBlocks(std::move(extents), kAllocatedBlocks);
}

// Test streaming too many blocks using the vector iterator.
TEST(VectorExtentIteratorTest, StreamBlocksInvalidLength) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  constexpr size_t kAllocatedExtents = 10;
  constexpr size_t kAllocatedBlocks = 10;
  constexpr size_t kAllocatedNodes = 1;

  TestSetup(kAllocatedBlocks, kAllocatedNodes, /* fragmented=*/true, &space_manager, &allocator);

  fbl::Vector<ReservedExtent> extents;
  ASSERT_EQ(allocator->ReserveBlocks(kAllocatedBlocks, &extents), ZX_OK);
  ASSERT_EQ(kAllocatedExtents, extents.size());

  BlockIterator iter(std::make_unique<VectorExtentIterator>(extents));

  size_t stream_blocks = 0;
  size_t stream_index = 0;
  auto stream_callback = [&](uint64_t local_offset, uint64_t dev_offset, uint32_t length) {
    ZX_DEBUG_ASSERT(stream_blocks == local_offset);
    ZX_DEBUG_ASSERT(extents[stream_index].extent().Start() == dev_offset);
    ZX_DEBUG_ASSERT(extents[stream_index].extent().Length() == length);

    stream_blocks += length;
    stream_index++;
    return ZX_OK;
  };

  // If we request more blocks than we allocated, streaming will fail.
  //
  // Up to the point of failure, however, we should still see only valid extents.
  ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY, StreamBlocks(&iter, kAllocatedBlocks + 10, stream_callback));
  ASSERT_TRUE(iter.Done());
}

}  // namespace
}  // namespace blobfs
