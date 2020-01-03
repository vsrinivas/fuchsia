// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage/operation/unbuffered_operations_builder.h"

#include <lib/zx/vmo.h>

#include <zxtest/zxtest.h>

namespace storage {
namespace {

constexpr size_t kVmoSize = 8192;

TEST(UnbufferedOperationsBuilderTest, NoRequest) {
  UnbufferedOperationsBuilder builder;
  EXPECT_EQ(0, builder.BlockCount());

  auto requests = builder.TakeOperations();

  EXPECT_TRUE(requests.is_empty());
  EXPECT_EQ(0, builder.BlockCount());
}

TEST(UnbufferedOperationsBuilderTest, EmptyRequest) {
  UnbufferedOperationsBuilder builder;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));

  UnbufferedOperation operation;
  operation.vmo = zx::unowned_vmo(vmo.get());
  operation.op.type = OperationType::kWrite;
  operation.op.vmo_offset = 0;
  operation.op.dev_offset = 0;
  operation.op.length = 0;
  builder.Add(operation);
  EXPECT_EQ(0, builder.BlockCount());

  auto requests = builder.TakeOperations();
  EXPECT_EQ(0, BlockCount(requests));
  EXPECT_TRUE(requests.is_empty());
}

TEST(UnbufferedOperationsBuilderTest, OneRequest) {
  UnbufferedOperationsBuilder builder;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));

  UnbufferedOperation operation;
  operation.vmo = zx::unowned_vmo(vmo.get());
  operation.op.type = OperationType::kWrite;
  operation.op.vmo_offset = 0;
  operation.op.dev_offset = 0;
  operation.op.length = 1;
  builder.Add(operation);
  ASSERT_EQ(1, builder.BlockCount());

  auto requests = builder.TakeOperations();
  ASSERT_EQ(1, BlockCount(requests));
  ASSERT_EQ(1, requests.size());
  EXPECT_EQ(requests[0].vmo->get(), vmo.get());
  EXPECT_EQ(requests[0].op.vmo_offset, operation.op.vmo_offset);
  EXPECT_EQ(requests[0].op.dev_offset, operation.op.dev_offset);
  EXPECT_EQ(requests[0].op.length, operation.op.length);
  EXPECT_EQ(0, builder.BlockCount());
}

TEST(UnbufferedOperationsBuilderTest, TwoRequestsDifferentVmos) {
  UnbufferedOperationsBuilder builder;

  zx::vmo vmos[2];
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmos[0]));
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmos[1]));

  UnbufferedOperation operations[2];
  operations[0].vmo = zx::unowned_vmo(vmos[0].get());
  operations[0].op.type = OperationType::kWrite;
  operations[0].op.vmo_offset = 0;
  operations[0].op.dev_offset = 0;
  operations[0].op.length = 1;
  builder.Add(operations[0]);
  operations[1].vmo = zx::unowned_vmo(vmos[1].get());
  operations[1].op.type = OperationType::kWrite;
  operations[1].op.vmo_offset = 1;
  operations[1].op.dev_offset = 1;
  operations[1].op.length = 2;
  builder.Add(operations[1]);
  EXPECT_EQ(3, builder.BlockCount());
  auto requests = builder.TakeOperations();
  EXPECT_EQ(3, BlockCount(requests));
  ASSERT_EQ(2, requests.size());

  for (size_t i = 0; i < 2; i++) {
    EXPECT_EQ(requests[i].vmo->get(), vmos[i].get());
    EXPECT_EQ(requests[i].op.vmo_offset, operations[i].op.vmo_offset);
    EXPECT_EQ(requests[i].op.dev_offset, operations[i].op.dev_offset);
    EXPECT_EQ(requests[i].op.length, operations[i].op.length);
  }
}

TEST(UnbufferedOperationsBuilderTest, TwoRequestsSameVmoUnalignedVmoOffset) {
  UnbufferedOperationsBuilder builder;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));

  UnbufferedOperation operations[2];
  operations[0].vmo = zx::unowned_vmo(vmo.get());
  operations[0].op.type = OperationType::kWrite;
  operations[0].op.vmo_offset = 0;
  operations[0].op.dev_offset = 0;
  operations[0].op.length = 1;
  builder.Add(operations[0]);
  operations[1].vmo = zx::unowned_vmo(vmo.get());
  operations[1].op.type = OperationType::kWrite;
  operations[1].op.vmo_offset = 2;
  operations[1].op.dev_offset = 1;
  operations[1].op.length = 2;
  builder.Add(operations[1]);
  EXPECT_EQ(3, builder.BlockCount());
  auto requests = builder.TakeOperations();
  EXPECT_EQ(3, BlockCount(requests));
  ASSERT_EQ(2, requests.size());

  for (size_t i = 0; i < 2; i++) {
    EXPECT_EQ(requests[i].vmo->get(), vmo.get());
    EXPECT_EQ(requests[i].op.vmo_offset, operations[i].op.vmo_offset);
    EXPECT_EQ(requests[i].op.dev_offset, operations[i].op.dev_offset);
    EXPECT_EQ(requests[i].op.length, operations[i].op.length);
  }
}

TEST(UnbufferedOperationsBuilderTest, TwoRequestsSameVmoUnalignedVmoOffsetReverseOrder) {
  UnbufferedOperationsBuilder builder;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));

  UnbufferedOperation operations[2];
  operations[0].vmo = zx::unowned_vmo(vmo.get());
  operations[0].op.type = OperationType::kWrite;
  operations[0].op.vmo_offset = 2;
  operations[0].op.dev_offset = 1;
  operations[0].op.length = 2;
  builder.Add(operations[0]);
  operations[1].vmo = zx::unowned_vmo(vmo.get());
  operations[1].op.type = OperationType::kWrite;
  operations[1].op.vmo_offset = 0;
  operations[1].op.dev_offset = 0;
  operations[1].op.length = 1;
  builder.Add(operations[1]);
  EXPECT_EQ(3, builder.BlockCount());

  auto requests = builder.TakeOperations();
  ASSERT_EQ(2, requests.size());

  for (size_t i = 0; i < 2; i++) {
    EXPECT_EQ(requests[i].vmo->get(), vmo.get());
    EXPECT_EQ(requests[i].op.vmo_offset, operations[i].op.vmo_offset);
    EXPECT_EQ(requests[i].op.dev_offset, operations[i].op.dev_offset);
    EXPECT_EQ(requests[i].op.length, operations[i].op.length);
  }
}

TEST(UnbufferedOperationsBuilderTest, TwoRequestsSameVmoUnalignedDevOffset) {
  UnbufferedOperationsBuilder builder;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));

  UnbufferedOperation operations[2];
  operations[0].vmo = zx::unowned_vmo(vmo.get());
  operations[0].op.type = OperationType::kWrite;
  operations[0].op.vmo_offset = 0;
  operations[0].op.dev_offset = 0;
  operations[0].op.length = 1;
  builder.Add(operations[0]);
  operations[1].vmo = zx::unowned_vmo(vmo.get());
  operations[1].op.type = OperationType::kWrite;
  operations[1].op.vmo_offset = 1;
  operations[1].op.dev_offset = 2;
  operations[1].op.length = 2;
  builder.Add(operations[1]);
  EXPECT_EQ(3, builder.BlockCount());

  auto requests = builder.TakeOperations();
  ASSERT_EQ(2, requests.size());

  for (size_t i = 0; i < 2; i++) {
    EXPECT_EQ(requests[i].vmo->get(), vmo.get());
    EXPECT_EQ(requests[i].op.vmo_offset, operations[i].op.vmo_offset);
    EXPECT_EQ(requests[i].op.dev_offset, operations[i].op.dev_offset);
    EXPECT_EQ(requests[i].op.length, operations[i].op.length);
  }
}

TEST(UnbufferedOperationsBuilderTest, TwoRequestsSameVmoUnalignedDevOffsetReverseOrder) {
  UnbufferedOperationsBuilder builder;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));

  UnbufferedOperation operations[2];
  operations[0].vmo = zx::unowned_vmo(vmo.get());
  operations[0].op.type = OperationType::kWrite;
  operations[0].op.vmo_offset = 1;
  operations[0].op.dev_offset = 2;
  operations[0].op.length = 2;
  builder.Add(operations[0]);
  operations[1].vmo = zx::unowned_vmo(vmo.get());
  operations[1].op.type = OperationType::kWrite;
  operations[1].op.vmo_offset = 0;
  operations[1].op.dev_offset = 0;
  operations[1].op.length = 1;
  builder.Add(operations[1]);
  EXPECT_EQ(3, builder.BlockCount());

  auto requests = builder.TakeOperations();
  ASSERT_EQ(2, requests.size());

  for (size_t i = 0; i < 2; i++) {
    EXPECT_EQ(requests[i].vmo->get(), vmo.get());
    EXPECT_EQ(requests[i].op.vmo_offset, operations[i].op.vmo_offset);
    EXPECT_EQ(requests[i].op.dev_offset, operations[i].op.dev_offset);
    EXPECT_EQ(requests[i].op.length, operations[i].op.length);
  }
}

TEST(UnbufferedOperationsBuilderTest, TwoRequestsSameVmoDifferentTypes) {
  UnbufferedOperationsBuilder builder;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));

  UnbufferedOperation operations[2];
  operations[0].vmo = zx::unowned_vmo(vmo.get());
  operations[0].op.type = OperationType::kWrite;
  operations[0].op.vmo_offset = 0;
  operations[0].op.dev_offset = 0;
  operations[0].op.length = 1;
  builder.Add(operations[0]);
  operations[1].vmo = zx::unowned_vmo(vmo.get());
  operations[1].op.type = OperationType::kRead;
  operations[1].op.vmo_offset = 1;
  operations[1].op.dev_offset = 1;
  operations[1].op.length = 2;
  builder.Add(operations[1]);
  EXPECT_EQ(3, builder.BlockCount());

  auto requests = builder.TakeOperations();
  ASSERT_EQ(2, requests.size());

  EXPECT_EQ(requests[0].vmo->get(), vmo.get());
  EXPECT_EQ(requests[0].op.type, operations[0].op.type);
  EXPECT_EQ(requests[0].op.vmo_offset, operations[0].op.vmo_offset);
  EXPECT_EQ(requests[0].op.dev_offset, operations[0].op.dev_offset);
  EXPECT_EQ(requests[0].op.length, operations[0].op.length);

  EXPECT_EQ(requests[1].vmo->get(), vmo.get());
  EXPECT_EQ(requests[1].op.type, operations[1].op.type);
  EXPECT_EQ(requests[1].op.vmo_offset, operations[1].op.vmo_offset);
  EXPECT_EQ(requests[1].op.dev_offset, operations[1].op.dev_offset);
  EXPECT_EQ(requests[1].op.length, operations[1].op.length);
}

TEST(UnbufferedOperationsBuilderTest, TwoRequestsSameVmoDifferentStartCoalesced) {
  UnbufferedOperationsBuilder builder;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));

  UnbufferedOperation operations[2];
  operations[0].vmo = zx::unowned_vmo(vmo.get());
  operations[0].op.type = OperationType::kWrite;
  operations[0].op.vmo_offset = 0;
  operations[0].op.dev_offset = 0;
  operations[0].op.length = 1;
  builder.Add(operations[0]);
  operations[1].vmo = zx::unowned_vmo(vmo.get());
  operations[1].op.type = OperationType::kWrite;
  operations[1].op.vmo_offset = 1;
  operations[1].op.dev_offset = 1;
  operations[1].op.length = 2;
  builder.Add(operations[1]);
  EXPECT_EQ(3, builder.BlockCount());

  auto requests = builder.TakeOperations();
  ASSERT_EQ(1, requests.size());

  EXPECT_EQ(requests[0].vmo->get(), vmo.get());
  EXPECT_EQ(requests[0].op.vmo_offset, operations[0].op.vmo_offset);
  EXPECT_EQ(requests[0].op.dev_offset, operations[0].op.dev_offset);
  EXPECT_EQ(requests[0].op.length, operations[0].op.length + operations[1].op.length);
}

TEST(UnbufferedOperationsBuilderTest, TwoRequestsSameVmoDifferentStartCoalescedReverseOrder) {
  UnbufferedOperationsBuilder builder;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));

  UnbufferedOperation operations[2];
  operations[0].vmo = zx::unowned_vmo(vmo.get());
  operations[0].op.type = OperationType::kWrite;
  operations[0].op.vmo_offset = 1;
  operations[0].op.dev_offset = 1;
  operations[0].op.length = 2;
  builder.Add(operations[0]);
  operations[1].vmo = zx::unowned_vmo(vmo.get());
  operations[1].op.type = OperationType::kWrite;
  operations[1].op.vmo_offset = 0;
  operations[1].op.dev_offset = 0;
  operations[1].op.length = 1;
  builder.Add(operations[1]);
  EXPECT_EQ(3, builder.BlockCount());

  auto requests = builder.TakeOperations();
  ASSERT_EQ(1, requests.size());

  EXPECT_EQ(requests[0].vmo->get(), vmo.get());
  EXPECT_EQ(requests[0].op.vmo_offset, operations[1].op.vmo_offset);
  EXPECT_EQ(requests[0].op.dev_offset, operations[1].op.dev_offset);
  EXPECT_EQ(requests[0].op.length, operations[0].op.length + operations[1].op.length);
}

TEST(UnbufferedOperationsBuilderTest, TwoRequestsSameVmoDifferentStartPartialCoalesced) {
  UnbufferedOperationsBuilder builder;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));

  UnbufferedOperation operations[2];
  operations[0].vmo = zx::unowned_vmo(vmo.get());
  operations[0].op.type = OperationType::kWrite;
  operations[0].op.vmo_offset = 0;
  operations[0].op.dev_offset = 0;
  operations[0].op.length = 2;
  builder.Add(operations[0]);
  operations[1].vmo = zx::unowned_vmo(vmo.get());
  operations[1].op.type = OperationType::kWrite;
  operations[1].op.vmo_offset = 1;
  operations[1].op.dev_offset = 1;
  operations[1].op.length = 2;
  builder.Add(operations[1]);
  EXPECT_EQ(3, builder.BlockCount());

  auto requests = builder.TakeOperations();
  ASSERT_EQ(1, requests.size());

  EXPECT_EQ(requests[0].vmo->get(), vmo.get());
  EXPECT_EQ(requests[0].op.vmo_offset, operations[0].op.vmo_offset);
  EXPECT_EQ(requests[0].op.dev_offset, operations[0].op.dev_offset);
  EXPECT_EQ(requests[0].op.length, 3);
}

TEST(UnbufferedOperationsBuilderTest,
     TwoRequestsSameVmoDifferentStartPartialCoalescedReverseOrder) {
  UnbufferedOperationsBuilder builder;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));

  UnbufferedOperation operations[2];
  operations[0].vmo = zx::unowned_vmo(vmo.get());
  operations[0].op.type = OperationType::kWrite;
  operations[0].op.vmo_offset = 1;
  operations[0].op.dev_offset = 1;
  operations[0].op.length = 2;
  builder.Add(operations[0]);
  operations[1].vmo = zx::unowned_vmo(vmo.get());
  operations[1].op.type = OperationType::kWrite;
  operations[1].op.vmo_offset = 0;
  operations[1].op.dev_offset = 0;
  operations[1].op.length = 2;
  builder.Add(operations[1]);
  EXPECT_EQ(3, builder.BlockCount());

  auto requests = builder.TakeOperations();
  ASSERT_EQ(1, requests.size());

  EXPECT_EQ(requests[0].vmo->get(), vmo.get());
  EXPECT_EQ(requests[0].op.vmo_offset, operations[1].op.vmo_offset);
  EXPECT_EQ(requests[0].op.dev_offset, operations[1].op.dev_offset);
  EXPECT_EQ(requests[0].op.length, 3);
}

TEST(UnbufferedOperationsBuilderTest, TwoRequestsSameVmoSameStartCoalesced) {
  UnbufferedOperationsBuilder builder;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));

  UnbufferedOperation operations[2];
  operations[0].vmo = zx::unowned_vmo(vmo.get());
  operations[0].op.type = OperationType::kWrite;
  operations[0].op.vmo_offset = 0;
  operations[0].op.dev_offset = 0;
  operations[0].op.length = 1;
  builder.Add(operations[0]);
  operations[1].vmo = zx::unowned_vmo(vmo.get());
  operations[1].op.type = OperationType::kWrite;
  operations[1].op.vmo_offset = 0;
  operations[1].op.dev_offset = 0;
  operations[1].op.length = 2;
  builder.Add(operations[1]);
  ASSERT_EQ(2, builder.BlockCount());

  auto requests = builder.TakeOperations();
  ASSERT_EQ(1, requests.size());

  EXPECT_EQ(requests[0].vmo->get(), vmo.get());
  EXPECT_EQ(requests[0].op.vmo_offset, operations[0].op.vmo_offset);
  EXPECT_EQ(requests[0].op.dev_offset, operations[0].op.dev_offset);
  EXPECT_EQ(requests[0].op.length, operations[1].op.length);
}

TEST(UnbufferedOperationsBuilderTest, TwoRequestsSameVmoSameStartCoalescedReverseOrder) {
  UnbufferedOperationsBuilder builder;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));

  UnbufferedOperation operations[2];
  operations[0].vmo = zx::unowned_vmo(vmo.get());
  operations[0].op.type = OperationType::kWrite;
  operations[0].op.vmo_offset = 0;
  operations[0].op.dev_offset = 0;
  operations[0].op.length = 2;
  builder.Add(operations[0]);
  operations[1].vmo = zx::unowned_vmo(vmo.get());
  operations[1].op.type = OperationType::kWrite;
  operations[1].op.vmo_offset = 0;
  operations[1].op.dev_offset = 0;
  operations[1].op.length = 1;
  builder.Add(operations[1]);
  ASSERT_EQ(2, builder.BlockCount());

  auto requests = builder.TakeOperations();
  ASSERT_EQ(1, requests.size());

  EXPECT_EQ(requests[0].vmo->get(), vmo.get());
  EXPECT_EQ(requests[0].op.vmo_offset, operations[0].op.vmo_offset);
  EXPECT_EQ(requests[0].op.dev_offset, operations[0].op.dev_offset);
  EXPECT_EQ(requests[0].op.length, operations[0].op.length);
}

TEST(UnbufferedOperationsBuilderTest, TwoRequestsSameVmoSubsumeRequest) {
  UnbufferedOperationsBuilder builder;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));

  UnbufferedOperation operations[2];
  operations[0].vmo = zx::unowned_vmo(vmo.get());
  operations[0].op.type = OperationType::kWrite;
  operations[0].op.vmo_offset = 1;
  operations[0].op.dev_offset = 1;
  operations[0].op.length = 1;
  builder.Add(operations[0]);
  operations[1].vmo = zx::unowned_vmo(vmo.get());
  operations[1].op.type = OperationType::kWrite;
  operations[1].op.vmo_offset = 0;
  operations[1].op.dev_offset = 0;
  operations[1].op.length = 3;
  builder.Add(operations[1]);
  EXPECT_EQ(3, builder.BlockCount());

  auto requests = builder.TakeOperations();
  ASSERT_EQ(1, requests.size());

  EXPECT_EQ(requests[0].vmo->get(), vmo.get());
  EXPECT_EQ(requests[0].op.vmo_offset, operations[1].op.vmo_offset);
  EXPECT_EQ(requests[0].op.dev_offset, operations[1].op.dev_offset);
  EXPECT_EQ(requests[0].op.length, operations[1].op.length);
}

TEST(UnbufferedOperationsBuilderTest, TwoRequestsSameVmoSubsumeRequestReverse) {
  UnbufferedOperationsBuilder builder;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));

  UnbufferedOperation operations[2];
  operations[0].vmo = zx::unowned_vmo(vmo.get());
  operations[0].op.type = OperationType::kWrite;
  operations[0].op.vmo_offset = 0;
  operations[0].op.dev_offset = 0;
  operations[0].op.length = 3;
  builder.Add(operations[0]);
  operations[1].vmo = zx::unowned_vmo(vmo.get());
  operations[1].op.type = OperationType::kWrite;
  operations[1].op.vmo_offset = 1;
  operations[1].op.dev_offset = 1;
  operations[1].op.length = 1;
  builder.Add(operations[1]);
  EXPECT_EQ(3, builder.BlockCount());

  auto requests = builder.TakeOperations();
  ASSERT_EQ(1, requests.size());

  EXPECT_EQ(requests[0].vmo->get(), vmo.get());
  EXPECT_EQ(requests[0].op.vmo_offset, operations[0].op.vmo_offset);
  EXPECT_EQ(requests[0].op.dev_offset, operations[0].op.dev_offset);
  EXPECT_EQ(requests[0].op.length, operations[0].op.length);
}

TEST(UnbufferedOperationsBuilderTest, RequestCoalescedWithOnlyOneOfTwoMergableRequests) {
  UnbufferedOperationsBuilder builder;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));

  UnbufferedOperation operations[3];
  operations[0].vmo = zx::unowned_vmo(vmo.get());
  operations[0].op.type = OperationType::kWrite;
  operations[0].op.vmo_offset = 0;
  operations[0].op.dev_offset = 0;
  operations[0].op.length = 3;

  operations[1].vmo = zx::unowned_vmo(vmo.get());
  operations[1].op.type = OperationType::kWrite;
  operations[1].op.vmo_offset = 5;
  operations[1].op.dev_offset = 5;
  operations[1].op.length = 3;

  // operation two has range that overlaps with operation[0] and operation[1].
  operations[2].vmo = zx::unowned_vmo(vmo.get());
  operations[2].op.type = OperationType::kWrite;
  operations[2].op.vmo_offset = 2;
  operations[2].op.dev_offset = 2;
  operations[2].op.length = 4;

  builder.Add(operations[0]);
  EXPECT_EQ(3, builder.BlockCount());
  builder.Add(operations[1]);
  EXPECT_EQ(6, builder.BlockCount());
  builder.Add(operations[2]);
  EXPECT_EQ(9, builder.BlockCount());

  // operation[2] two can be coalesced with either operation[0] or with operation[1].
  // First added operation is preferred.
  auto requests = builder.TakeOperations();
  ASSERT_EQ(2, requests.size());

  // operations[0] was Added first. So operation[2] should have been coalesced with operation[0].
  EXPECT_EQ(requests[0].vmo->get(), vmo.get());
  EXPECT_EQ(requests[0].op.vmo_offset, operations[0].op.vmo_offset);
  EXPECT_EQ(requests[0].op.dev_offset, operations[0].op.dev_offset);
  EXPECT_EQ(requests[0].op.length, operations[0].op.length + operations[2].op.length - 1);

  EXPECT_EQ(requests[1].vmo->get(), vmo.get());
  EXPECT_EQ(requests[1].op.vmo_offset, operations[1].op.vmo_offset);
  EXPECT_EQ(requests[1].op.dev_offset, operations[1].op.dev_offset);
  EXPECT_EQ(requests[1].op.length, operations[1].op.length);

  // Flip the order of Add. Now operation[2] should be coalesced with operation[1]
  builder.Add(operations[1]);
  EXPECT_EQ(3, builder.BlockCount());
  builder.Add(operations[0]);
  EXPECT_EQ(6, builder.BlockCount());
  builder.Add(operations[2]);
  EXPECT_EQ(9, builder.BlockCount());

  // operation[2] two can be coalesced with either operation[0] or with operation[1].
  // First added operation is preferred.
  requests = builder.TakeOperations();
  ASSERT_EQ(2, requests.size());

  // operations[1] was Added first. So operation[2] should have been coalesced with operation[1].
  EXPECT_EQ(requests[0].vmo->get(), vmo.get());
  EXPECT_EQ(requests[0].op.vmo_offset, operations[2].op.vmo_offset);
  EXPECT_EQ(requests[0].op.dev_offset, operations[2].op.dev_offset);
  EXPECT_EQ(requests[0].op.length, operations[1].op.length + operations[2].op.length - 1);

  EXPECT_EQ(requests[1].vmo->get(), vmo.get());
  EXPECT_EQ(requests[1].op.vmo_offset, operations[0].op.vmo_offset);
  EXPECT_EQ(requests[1].op.dev_offset, operations[0].op.dev_offset);
  EXPECT_EQ(requests[1].op.length, operations[0].op.length);
}

}  // namespace
}  // namespace storage
