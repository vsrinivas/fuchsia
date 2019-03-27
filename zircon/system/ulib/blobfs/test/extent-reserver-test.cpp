// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <bitmap/rle-bitmap.h>
#include <blobfs/extent-reserver.h>
#include <blobfs/node-reserver.h>
#include <unittest/unittest.h>

namespace blobfs {
namespace {

// Test simple cases of reserving a single extent
bool ReserveTest() {
    BEGIN_TEST;
    ExtentReserver reserver;
    BlockOffsetType start_block = 0;
    BlockCountType block_count = 1;
    Extent extent(start_block, block_count);

    // The ReservedExtent constructor should reserve the extent.
    // The destructor should release the extent.
    {
        ReservedExtent reserved_extent(&reserver, extent);
        EXPECT_EQ(block_count, reserver.ReservedBlockCount());
    }
    EXPECT_EQ(0, reserver.ReservedBlockCount());

    END_TEST;
}

bool ReserveResetTest() {
    BEGIN_TEST;
    ExtentReserver reserver;
    BlockOffsetType start_block = 0;
    BlockCountType block_count = 1;
    Extent extent(start_block, block_count);

    // The ReservedExtent constructor should reserve the extent.
    // Reset should release the extent.
    {
        ReservedExtent reserved_extent(&reserver, extent);
        EXPECT_EQ(block_count, reserver.ReservedBlockCount());
        reserved_extent.Reset();
        EXPECT_EQ(0, reserver.ReservedBlockCount());
    }
    EXPECT_EQ(0, reserver.ReservedBlockCount());

    END_TEST;
}

// Test the constructors of the reserved extent.
bool ConstructorTest() {
    BEGIN_TEST;

    ExtentReserver reserver;
    BlockOffsetType start_block = 0;
    BlockCountType block_count = 1;
    Extent extent(start_block, block_count);

    // Test reservation via the constructor.
    {
        ReservedExtent reserved_extent(&reserver, extent);
        EXPECT_EQ(extent.Start(), reserved_extent.extent().Start());
        EXPECT_EQ(extent.Length(), reserved_extent.extent().Length());
        EXPECT_EQ(block_count, reserver.ReservedBlockCount());
    }
    EXPECT_EQ(0, reserver.ReservedBlockCount());
    END_TEST;
}

bool MoveConstructorTest() {
    BEGIN_TEST;

    ExtentReserver reserver;
    BlockOffsetType start_block = 0;
    BlockCountType block_count = 1;
    Extent extent(start_block, block_count);

    // Test reservation via move constructor.
    {
        ReservedExtent source_extent(&reserver, extent);
        EXPECT_EQ(1, reserver.ReservedBlockCount());
        EXPECT_EQ(extent.Start(), source_extent.extent().Start());
        EXPECT_EQ(extent.Length(), source_extent.extent().Length());

        ReservedExtent dest_extent(std::move(source_extent));
        EXPECT_EQ(1, reserver.ReservedBlockCount());
        EXPECT_EQ(extent.Start(), dest_extent.extent().Start());
        EXPECT_EQ(extent.Length(), dest_extent.extent().Length());
    }
    EXPECT_EQ(0, reserver.ReservedBlockCount());
    END_TEST;
}

bool MoveAssignmentTest() {
    BEGIN_TEST;

    ExtentReserver reserver;
    BlockOffsetType start_block = 0;
    BlockCountType block_count = 1;
    Extent extent(start_block, block_count);

    // Test reservation via the move assignment operator.
    {
        ReservedExtent source_extent(&reserver, extent);
        EXPECT_EQ(1, reserver.ReservedBlockCount());
        EXPECT_EQ(extent.Start(), source_extent.extent().Start());
        EXPECT_EQ(extent.Length(), source_extent.extent().Length());

        ReservedExtent dest_extent = std::move(source_extent);
        EXPECT_EQ(1, reserver.ReservedBlockCount());
        EXPECT_EQ(extent.Start(), dest_extent.extent().Start());
        EXPECT_EQ(extent.Length(), dest_extent.extent().Length());
    }

    END_TEST;
}

// Test splitting of extents.
bool SplitTest() {
    BEGIN_TEST;

    ExtentReserver reserver;
    uint64_t start_block = 0;
    BlockCountType block_count = 10;
    Extent extent { start_block, block_count };

    EXPECT_EQ(0, reserver.ReservedBlockCount());
    ReservedExtent reserved_extent(&reserver, extent);
    EXPECT_EQ(10, reserver.ReservedBlockCount());

    {
        const BlockCountType split_point = 5;
        ReservedExtent latter(reserved_extent.SplitAt(split_point));
        // After splitting, no reservations actually change.
        EXPECT_EQ(10, reserver.ReservedBlockCount());

        // Verify the split extents contain the expected values.
        EXPECT_EQ(extent.Start(), reserved_extent.extent().Start());
        EXPECT_EQ(split_point, reserved_extent.extent().Length());

        EXPECT_EQ(extent.Start() + split_point, latter.extent().Start());
        EXPECT_EQ(block_count - split_point, latter.extent().Length());
    }

    // When the latter half of the reservation goes out of scope, the reservations
    // are cleaned up too.
    EXPECT_EQ(5, reserver.ReservedBlockCount());

    END_TEST;
}

} // namespace
} // namespace blobfs

BEGIN_TEST_CASE(blobfsExtentReserverTests)
RUN_TEST(blobfs::ReserveTest)
RUN_TEST(blobfs::ReserveResetTest)
RUN_TEST(blobfs::ConstructorTest)
RUN_TEST(blobfs::MoveConstructorTest)
RUN_TEST(blobfs::MoveAssignmentTest)
RUN_TEST(blobfs::SplitTest)
END_TEST_CASE(blobfsExtentReserverTests)
