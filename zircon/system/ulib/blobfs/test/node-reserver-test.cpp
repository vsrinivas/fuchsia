// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <bitmap/rle-bitmap.h>
#include <blobfs/extent-reserver.h>
#include <blobfs/node-reserver.h>
#include <unittest/unittest.h>

namespace blobfs {
namespace {

// Test that reserving a node actually changes the node count, and that RAII releases the node.
bool ReserveTest() {
    BEGIN_TEST;

    NodeReserver reserver;
    {
        const uint32_t ino = 3;
        ReservedNode reserved_node(&reserver, ino);
        EXPECT_EQ(1, reserver.ReservedNodeCount());
    }
    EXPECT_EQ(0, reserver.ReservedNodeCount());
    END_TEST;
}

bool ReserveResetTest() {
    BEGIN_TEST;

    NodeReserver reserver;
    {
        const uint32_t ino = 3;
        ReservedNode reserved_node(&reserver, ino);
        EXPECT_EQ(1, reserver.ReservedNodeCount());
        reserved_node.Reset();
        EXPECT_EQ(0, reserver.ReservedNodeCount());
    }
    EXPECT_EQ(0, reserver.ReservedNodeCount());

    END_TEST;
}

// Test the constructors of the reserved node.
bool ConstructorTest() {
    BEGIN_TEST;

    NodeReserver reserver;
    // Test the constructor.
    {
        ReservedNode reserved_node(&reserver, 3);
        EXPECT_EQ(3, reserved_node.index());
        EXPECT_EQ(1, reserver.ReservedNodeCount());
    }
    EXPECT_EQ(0, reserver.ReservedNodeCount());
    END_TEST;
}

bool MoveConstructorTest() {
    BEGIN_TEST;

    NodeReserver reserver;
    // Test the move constructor.
    {
        ReservedNode reserved_node(&reserver, 3);
        EXPECT_EQ(3, reserved_node.index());
        EXPECT_EQ(1, reserver.ReservedNodeCount());

        ReservedNode dest_node(std::move(reserved_node));
        EXPECT_EQ(3, dest_node.index());
        EXPECT_EQ(1, reserver.ReservedNodeCount());
    }
    EXPECT_EQ(0, reserver.ReservedNodeCount());
    END_TEST;
}

bool MoveAssignmentTest() {
    BEGIN_TEST;

    NodeReserver reserver;
    // Test the move assignment operator.
    {
        ReservedNode reserved_node(&reserver, 3);
        EXPECT_EQ(3, reserved_node.index());
        EXPECT_EQ(1, reserver.ReservedNodeCount());

        ReservedNode dest_node = std::move(reserved_node);
        EXPECT_EQ(3, dest_node.index());
        EXPECT_EQ(1, reserver.ReservedNodeCount());
    }
    EXPECT_EQ(0, reserver.ReservedNodeCount());

    END_TEST;
}

} // namespace
} // namespace blobfs

BEGIN_TEST_CASE(blobfsNodeReserverTests)
RUN_TEST(blobfs::ReserveTest)
RUN_TEST(blobfs::ReserveResetTest)
RUN_TEST(blobfs::ConstructorTest)
RUN_TEST(blobfs::MoveConstructorTest)
RUN_TEST(blobfs::MoveAssignmentTest)
END_TEST_CASE(blobfsNodeReserverTests)
