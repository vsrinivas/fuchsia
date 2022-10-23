// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/allocator/node_reserver.h"

#include <lib/zx/result.h>

#include <memory>

#include <bitmap/rle-bitmap.h>
#include <gtest/gtest.h>
#include <id_allocator/id_allocator.h>
#include <safemath/checked_math.h>

#include "src/storage/blobfs/allocator/extent_reserver.h"

namespace blobfs {
namespace {

class FakeNodeReserver : public NodeReserverInterface {
 public:
  explicit FakeNodeReserver(uint32_t node_count) {
    ZX_ASSERT(id_allocator::IdAllocator::Create(node_count, &node_bitmap_) == ZX_OK);
  }

  zx::result<ReservedNode> ReserveNode() override {
    zx_status_t status;
    size_t index;
    if ((status = node_bitmap_->Allocate(&index)) != ZX_OK) {
      return zx::error(status);
    }
    ++reserved_node_count_;
    return zx::ok(ReservedNode(this, safemath::checked_cast<uint32_t>(index)));
  }

  void UnreserveNode(ReservedNode node) override {
    // Catch duplicate calls to |UnreserveNode|.
    ASSERT_TRUE(node_bitmap_->IsBusy(node.index()));
    ASSERT_EQ(node_bitmap_->Free(node.index()), ZX_OK);
    node.Release();
    --reserved_node_count_;
  }

  uint64_t ReservedNodeCount() const override { return reserved_node_count_; }

  bool IsNodeReserved(uint32_t node_index) { return node_bitmap_->IsBusy(node_index); }

 private:
  std::unique_ptr<id_allocator::IdAllocator> node_bitmap_;
  uint32_t reserved_node_count_ = 0;
};

TEST(NodeReserver, DestructorUnreservesNode) {
  FakeNodeReserver reserver(/*node_count=*/1);
  uint32_t node_index;
  {
    auto node = reserver.ReserveNode();
    ASSERT_TRUE(node.is_ok());
    node_index = node->index();
    EXPECT_EQ(reserver.ReservedNodeCount(), 1u);
    EXPECT_TRUE(reserver.IsNodeReserved(node_index));
  }
  EXPECT_EQ(reserver.ReservedNodeCount(), 0u);
  EXPECT_FALSE(reserver.IsNodeReserved(node_index));
}

TEST(NodeReserver, ReleasePreventsNodeFromBeingUnreserved) {
  FakeNodeReserver reserver(/*node_count=*/1);
  uint32_t node_index;
  {
    auto node = reserver.ReserveNode();
    ASSERT_TRUE(node.is_ok());
    node_index = node->index();
    EXPECT_EQ(reserver.ReservedNodeCount(), 1u);
    EXPECT_TRUE(reserver.IsNodeReserved(node_index));
    node->Release();
  }
  EXPECT_EQ(reserver.ReservedNodeCount(), 1u);
  EXPECT_TRUE(reserver.IsNodeReserved(node_index));
}

TEST(NodeReserver, MoveConstructorReleasesMovedFromNode) {
  FakeNodeReserver reserver(/*node_count=*/1);
  {
    auto reserved_node = reserver.ReserveNode();
    ASSERT_TRUE(reserved_node.is_ok());
    uint32_t node_index = reserved_node->index();
    EXPECT_EQ(reserver.ReservedNodeCount(), 1u);

    ReservedNode dest_node(std::move(reserved_node).value());
    EXPECT_EQ(dest_node.index(), node_index);
    EXPECT_EQ(reserver.ReservedNodeCount(), 1u);

    // If the moved from node wasn't released then there would be 2 calls to |UnreserveNode| for the
    // same node.
  }
  EXPECT_EQ(reserver.ReservedNodeCount(), 0u);
}

TEST(NodeReserver, MoveAssignmentUnreservesSelfAndReleasesTheMovedFromNode) {
  FakeNodeReserver reserver(/*node_count=*/2);
  {
    auto node1 = reserver.ReserveNode();
    ASSERT_TRUE(node1.is_ok());
    uint32_t node1_index = node1->index();

    auto node2 = reserver.ReserveNode();
    ASSERT_TRUE(node2.is_ok());
    uint32_t node2_index = node2->index();

    EXPECT_NE(node1_index, node2_index);
    EXPECT_EQ(reserver.ReservedNodeCount(), 2u);

    node2.value() = std::move(node1).value();

    EXPECT_EQ(reserver.ReservedNodeCount(), 1u);
    EXPECT_FALSE(reserver.IsNodeReserved(node2_index));
    EXPECT_EQ(node2->index(), node1_index);
  }
  EXPECT_EQ(reserver.ReservedNodeCount(), 0u);
}

}  // namespace
}  // namespace blobfs
