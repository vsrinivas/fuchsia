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

class TestReserver : public NodeReserver {
public:
    bool IsReserved(uint32_t ino) const {
        return IsNodeReserved(ino);
    }

    uint32_t LowerBound() const {
        return FreeNodeLowerBound();
    }

    void SetBound(uint32_t ino) {
        SetFreeNodeLowerBound(ino);
    }

    void SetBoundIfSmallest(uint32_t ino) {
        SetFreeNodeLowerBoundIfSmallest(ino);
    }
};

// Test the protected interface available to subclasses of the node reserver.
bool LowerBoundAutoResetTest() {
    BEGIN_TEST;

    TestReserver reserver;

    // The lower bound should start at zero.
    EXPECT_FALSE(reserver.IsReserved(0));
    EXPECT_EQ(0, reserver.LowerBound());

    // The lower bound won't move unless we manually move it.
    ReservedNode node(&reserver, 0);
    EXPECT_TRUE(reserver.IsReserved(0));
    EXPECT_EQ(0, reserver.LowerBound());
    reserver.SetBound(3);
    EXPECT_EQ(3, reserver.LowerBound());

    // When we release a node with ino = 0, the lower bound moves back.
    node.Reset();
    EXPECT_EQ(0, reserver.LowerBound());

    END_TEST;
}

bool LowerBoundManualResetTest() {
    BEGIN_TEST;

    TestReserver reserver;

    // When we manually release a node (akin to freeing a committed but unreserved
    // node), the lower bound moves back.
    reserver.SetBound(3);
    EXPECT_EQ(3, reserver.LowerBound());
    reserver.SetBoundIfSmallest(1);
    EXPECT_EQ(1, reserver.LowerBound());

    // Releasing a higher index does nothing.
    reserver.SetBoundIfSmallest(10);
    EXPECT_EQ(1, reserver.LowerBound());

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
RUN_TEST(blobfs::LowerBoundAutoResetTest)
RUN_TEST(blobfs::LowerBoundManualResetTest)
END_TEST_CASE(blobfsNodeReserverTests);
