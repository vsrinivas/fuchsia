// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/iterator/vector-extent-iterator.h>
#include <blobfs/iterator/block-iterator.h>
#include <unittest/unittest.h>

#include "utils.h"

namespace blobfs {
namespace {

// Allocates a blob with the provided number of extents / nodes.
//
// Returns the allocator, the extents, and nodes used.
bool TestSetup(const size_t kAllocatedBlocks, const size_t kAllocatedNodes, bool fragmented,
               MockSpaceManager* space_manager, fbl::unique_ptr<Allocator>* out_allocator) {
    BEGIN_HELPER;

    // Block count is large enough to allow for both fragmentation and the
    // allocation of |kAllocatedBlocks| extents.
    const size_t kBlockCount = 3 * kAllocatedBlocks;
    ASSERT_TRUE(InitializeAllocator(kBlockCount, kAllocatedNodes, space_manager, out_allocator));
    if (fragmented) {
        ASSERT_TRUE(ForceFragmentation(out_allocator->get(), kBlockCount));
    }

    END_HELPER;
}

// Iterate over the null blob.
bool NullTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    fbl::Vector<Extent> allocated_extents;
    fbl::Vector<uint32_t> allocated_nodes;
    constexpr size_t kAllocatedExtents = 0;
    constexpr size_t kAllocatedNodes = 1;

    ASSERT_TRUE(TestSetup(kAllocatedExtents, kAllocatedNodes, /* fragmented=*/ true, &space_manager,
                          &allocator));

    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(kAllocatedExtents, &extents));
    ASSERT_EQ(0, extents.size());

    VectorExtentIterator iter(extents);

    ASSERT_TRUE(iter.Done());
    ASSERT_EQ(0, iter.BlockIndex());
    END_TEST;
}

// Iterate over a blob with some extents.
bool MultiExtentTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    fbl::Vector<Extent> allocated_extents;
    fbl::Vector<uint32_t> allocated_nodes;
    constexpr size_t kAllocatedExtents = 10;
    constexpr size_t kAllocatedNodes = 1;

    ASSERT_TRUE(TestSetup(kAllocatedExtents, kAllocatedNodes, /* fragmented=*/ true, &space_manager,
                          &allocator));

    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(kAllocatedExtents, &extents));
    ASSERT_EQ(kAllocatedExtents, extents.size());

    VectorExtentIterator iter(extents);

    size_t blocks_seen = 0;
    for (size_t i = 0; i < kAllocatedExtents; i++) {
        ASSERT_FALSE(iter.Done());

        const Extent* extent;
        ASSERT_EQ(ZX_OK, iter.Next(&extent));
        ASSERT_TRUE(extents[i].extent() == *extent);
        blocks_seen += extent->Length();
        ASSERT_EQ(blocks_seen, iter.BlockIndex());
    }

    ASSERT_TRUE(iter.Done());
    END_TEST;
}

// Test the usage of the BlockIterator over the vector extent iterator.
bool BlockIteratorTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    constexpr size_t kAllocatedExtents = 10;
    constexpr size_t kAllocatedNodes = 1;

    ASSERT_TRUE(TestSetup(kAllocatedExtents, kAllocatedNodes, /* fragmented=*/ true, &space_manager,
                          &allocator));

    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(kAllocatedExtents, &extents));
    ASSERT_EQ(kAllocatedExtents, extents.size());

    VectorExtentIterator vector_iter(extents);
    BlockIterator iter(&vector_iter);
    ASSERT_EQ(0, iter.BlockIndex());
    ASSERT_FALSE(iter.Done());

    uint32_t blocks_seen = 0;
    for (size_t i = 0; i < extents.size(); i++) {
        ASSERT_FALSE(iter.Done());
        uint32_t actual_length;
        uint64_t actual_start;
        ASSERT_EQ(ZX_OK, iter.Next(1, &actual_length, &actual_start));
        ASSERT_EQ(1, actual_length);
        ASSERT_EQ(extents[i].extent().Start(), actual_start);
        blocks_seen += actual_length;
        ASSERT_EQ(blocks_seen, iter.BlockIndex());
    }

    ASSERT_TRUE(iter.Done());
    END_TEST;
}

bool StreamBlocksValidator(fbl::Vector<ReservedExtent> extents, uint32_t block_count) {
    BEGIN_HELPER;
    VectorExtentIterator vector_iter(extents);
    BlockIterator block_iter(&vector_iter);

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

    ASSERT_EQ(ZX_OK, StreamBlocks(&block_iter, block_count, stream_callback));
    ASSERT_TRUE(block_iter.Done());
    END_HELPER;
}

// Test streaming blocks from a fragmented iterator.
bool StreamBlocksFragmentedTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    constexpr size_t kAllocatedExtents = 10;
    constexpr size_t kAllocatedBlocks = kAllocatedExtents;
    constexpr size_t kAllocatedNodes = 1;

    ASSERT_TRUE(TestSetup(kAllocatedBlocks, kAllocatedNodes, /* fragmented=*/ true,
                          &space_manager, &allocator));

    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(kAllocatedBlocks, &extents));
    ASSERT_EQ(kAllocatedExtents, extents.size());

    ASSERT_TRUE(StreamBlocksValidator(std::move(extents), kAllocatedBlocks));

    END_TEST;
}

// Test streaming blocks from a contiguous iterator.
bool StreamBlocksContiguousTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    constexpr size_t kAllocatedExtents = 1;
    constexpr size_t kAllocatedBlocks = 10;
    constexpr size_t kAllocatedNodes = 1;

    ASSERT_TRUE(TestSetup(kAllocatedBlocks, kAllocatedNodes, /* fragmented=*/ false,
                          &space_manager, &allocator));

    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(kAllocatedBlocks, &extents));
    ASSERT_EQ(kAllocatedExtents, extents.size());

    ASSERT_TRUE(StreamBlocksValidator(std::move(extents), kAllocatedBlocks));

    END_TEST;
}

// Test streaming too many blocks using the vector iterator.
bool StreamBlocksInvalidLengthTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    constexpr size_t kAllocatedExtents = 10;
    constexpr size_t kAllocatedBlocks = 10;
    constexpr size_t kAllocatedNodes = 1;

    ASSERT_TRUE(TestSetup(kAllocatedBlocks, kAllocatedNodes, /* fragmented=*/ true,
                          &space_manager, &allocator));

    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(kAllocatedBlocks, &extents));
    ASSERT_EQ(kAllocatedExtents, extents.size());

    VectorExtentIterator vector_iter(extents);
    BlockIterator block_iter(&vector_iter);

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
    ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY, StreamBlocks(&block_iter, kAllocatedBlocks + 10,
                                                     stream_callback));
    ASSERT_TRUE(block_iter.Done());
    END_TEST;
}

} // namespace
} // namespace blobfs

BEGIN_TEST_CASE(blobfsVectorExtentIteratorTests)
RUN_TEST(blobfs::NullTest)
RUN_TEST(blobfs::MultiExtentTest)
RUN_TEST(blobfs::BlockIteratorTest)
RUN_TEST(blobfs::StreamBlocksFragmentedTest)
RUN_TEST(blobfs::StreamBlocksContiguousTest)
RUN_TEST(blobfs::StreamBlocksInvalidLengthTest)
END_TEST_CASE(blobfsVectorExtentIteratorTests)
