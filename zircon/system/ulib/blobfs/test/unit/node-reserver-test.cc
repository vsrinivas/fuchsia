// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "allocator/node-reserver.h"

#include <bitmap/rle-bitmap.h>
#include <zxtest/zxtest.h>

#include "allocator/extent-reserver.h"

namespace blobfs {
namespace {

// Test that reserving a node actually changes the node count, and that RAII releases the node.
TEST(NodeReserver, Reserve) {
  NodeReserver reserver;
  {
    const uint32_t ino = 3;
    ReservedNode reserved_node(&reserver, ino);
    EXPECT_EQ(1, reserver.ReservedNodeCount());
  }
  EXPECT_EQ(0, reserver.ReservedNodeCount());
}

TEST(NodeReserver, ReserveReset) {
  NodeReserver reserver;
  {
    const uint32_t ino = 3;
    ReservedNode reserved_node(&reserver, ino);
    EXPECT_EQ(1, reserver.ReservedNodeCount());
    reserved_node.Reset();
    EXPECT_EQ(0, reserver.ReservedNodeCount());
  }
  EXPECT_EQ(0, reserver.ReservedNodeCount());
}

// Test the constructors of the reserved node.
TEST(NodeReserver, Constructor) {
  NodeReserver reserver;
  // Test the constructor.
  {
    ReservedNode reserved_node(&reserver, 3);
    EXPECT_EQ(3, reserved_node.index());
    EXPECT_EQ(1, reserver.ReservedNodeCount());
  }
  EXPECT_EQ(0, reserver.ReservedNodeCount());
}

TEST(NodeReserver, MoveConstructor) {
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
}

TEST(NodeReserver, MoveAssignment) {
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
}

}  // namespace
}  // namespace blobfs
