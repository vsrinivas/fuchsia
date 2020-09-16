// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "replay_tree.h"

#include <gtest/gtest.h>

namespace fs {
namespace {

constexpr vmoid_t kVmoid = 1;

storage::BufferedOperation MakeOperation(uint64_t vmo_offset, uint64_t dev_offset,
                                         uint64_t length) {
  storage::BufferedOperation operation;
  operation.vmoid = kVmoid;
  operation.op.type = storage::OperationType::kWrite;
  operation.op.vmo_offset = vmo_offset;
  operation.op.dev_offset = dev_offset;
  operation.op.length = length;
  return operation;
}

void ExpectOperationsEqual(const storage::BufferedOperation lhs,
                           const storage::BufferedOperation rhs) {
  EXPECT_EQ(lhs.vmoid, rhs.vmoid);
  EXPECT_EQ(lhs.op.type, rhs.op.type);
  EXPECT_EQ(lhs.op.vmo_offset, rhs.op.vmo_offset);
  EXPECT_EQ(lhs.op.dev_offset, rhs.op.dev_offset);
  EXPECT_EQ(lhs.op.length, rhs.op.length);
}

TEST(ReplayTreeTest, EmptyTreeDoesNothing) { ReplayTree tree; }

// Vmo offset: Contiguous
// Dev offset: Contiguous
// Result: Merge
TEST(ReplayTreeTest, ContiguousOperationsMerge) {
  ReplayTree tree;

  storage::BufferedOperation operation_a = MakeOperation(0, 0, 1);
  storage::BufferedOperation operation_b = MakeOperation(1, 1, 1);
  storage::BufferedOperation operation_merged = MakeOperation(0, 0, 2);

  tree.insert(operation_a);
  tree.insert(operation_b);

  ASSERT_EQ(tree.size(), 1ul);
  ExpectOperationsEqual(operation_merged, tree.begin()->second.container().operation);
}

// Vmo offset: Contiguous
// Dev offset: Not contiguous
// Result: No merge
TEST(ReplayTreeTest, NonContiguousDevOffsetsStaySeparate) {
  ReplayTree tree;

  storage::BufferedOperation operation_a = MakeOperation(0, 0, 1);
  storage::BufferedOperation operation_b = MakeOperation(1, 2, 1);

  tree.insert(operation_a);
  tree.insert(operation_b);

  ASSERT_EQ(tree.size(), 2ul);
  auto iter = tree.begin();
  ExpectOperationsEqual(operation_a, iter->second.container().operation);
  iter++;
  ExpectOperationsEqual(operation_b, iter->second.container().operation);
}

// Vmo offset: Not contiguous
// Dev offset: Contiguous
// Result: No merge
TEST(ReplayTreeTest, NonContiguousVmoOffsetsStaySeparate) {
  ReplayTree tree;

  storage::BufferedOperation operation_a = MakeOperation(0, 0, 1);
  storage::BufferedOperation operation_b = MakeOperation(2, 1, 1);

  tree.insert(operation_a);
  tree.insert(operation_b);

  ASSERT_EQ(tree.size(), 2ul);
  auto iter = tree.begin();
  ExpectOperationsEqual(operation_a, iter->second.container().operation);
  iter++;
  ExpectOperationsEqual(operation_b, iter->second.container().operation);
}

// Vmo offset: Different
// Dev offset: Same
// Result: Use latest
TEST(ReplayTreeTest, OverlappingDevOffsetTakesLatest) {
  ReplayTree tree;

  storage::BufferedOperation operation_a = MakeOperation(0, 0, 1);
  storage::BufferedOperation operation_b = MakeOperation(2, 0, 1);

  tree.insert(operation_a);
  tree.insert(operation_b);

  ASSERT_EQ(tree.size(), 1ul);
  ExpectOperationsEqual(operation_b, tree.begin()->second.container().operation);
}

// Vmo offset: Different
// Dev offset: Overlapping
// Result: Split prior operation
TEST(ReplayTreeTest, NonContiguousVmoOffsetUpdateBreaksMergedOperations) {
  ReplayTree tree;
  storage::BufferedOperation o1 = MakeOperation(0, 0, 1);
  storage::BufferedOperation o2 = MakeOperation(1, 1, 1);
  storage::BufferedOperation o3 = MakeOperation(2, 0, 1);
  tree.insert(o1);
  tree.insert(o2);
  tree.insert(o3);
  ASSERT_EQ(tree.size(), 2ul);
  auto iter = tree.begin();
  ExpectOperationsEqual(o3, iter->second.container().operation);
  iter++;
  ExpectOperationsEqual(o2, iter->second.container().operation);
  iter++;
}

}  // namespace
}  // namespace fs
