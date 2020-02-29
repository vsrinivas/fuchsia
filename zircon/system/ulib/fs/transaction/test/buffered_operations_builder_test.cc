// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs/transaction/buffered_operations_builder.h"

#include <storage/buffer/vmo_buffer.h>
#include <storage/buffer/vmoid_registry.h>
#include <zxtest/zxtest.h>

namespace {

using fs::BufferedOperationsBuilder;
using storage::Operation;
using storage::OperationType;
using storage::VmoBuffer;
using storage::VmoidRegistry;

const vmoid_t kVmoid1 = 5;
const vmoid_t kVmoid2 = 12;
const size_t kCapacity = 3;
const uint32_t kBlockSize = 8192;
constexpr char kLabel[] = "test-vmo";

TEST(BufferedOperationsBuilderTest, NoRequest) {
  BufferedOperationsBuilder builder(nullptr);

  auto requests = builder.TakeOperations();
  EXPECT_TRUE(requests.empty());
}

class MockVmoidRegistry : public VmoidRegistry {
 public:
  MockVmoidRegistry(vmoid_t vmoid = kVmoid1) : vmoid_(vmoid) {}

  zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) final {
    *out = vmoid_;
    return ZX_OK;
  }

  zx_status_t DetachVmo(vmoid_t vmoid) final {
    EXPECT_EQ(vmoid_, vmoid);
    return ZX_OK;
  }

 private:
  vmoid_t vmoid_;
};

TEST(BufferedOperationsBuilderTest, OneRequest) {
  BufferedOperationsBuilder builder(nullptr);

  MockVmoidRegistry registry;
  VmoBuffer buffer;
  buffer.Initialize(&registry, kCapacity, kBlockSize, kLabel);

  Operation operation;
  operation.type = OperationType::kWrite;
  operation.vmo_offset = 0;
  operation.dev_offset = 0;
  operation.length = 1;
  builder.Add(operation, &buffer);

  auto requests = builder.TakeOperations();
  ASSERT_EQ(1, requests.size());
  EXPECT_EQ(kVmoid1, requests[0].vmoid);
  EXPECT_EQ(OperationType::kWrite, requests[0].op.type);
  EXPECT_EQ(operation.vmo_offset, requests[0].op.vmo_offset);
  EXPECT_EQ(operation.dev_offset, requests[0].op.dev_offset);
  EXPECT_EQ(operation.length, requests[0].op.length);
}

TEST(BufferedOperationsBuilderTest, TwoRequestDifferentVmo) {
  BufferedOperationsBuilder builder(nullptr);

  MockVmoidRegistry registry_1;
  VmoBuffer buffer_1;
  buffer_1.Initialize(&registry_1, kCapacity, kBlockSize, kLabel);

  Operation operation;
  operation.type = OperationType::kWrite;
  operation.vmo_offset = 0;
  operation.dev_offset = 0;
  operation.length = 1;
  builder.Add(operation, &buffer_1);

  MockVmoidRegistry registry_2(kVmoid2);
  VmoBuffer buffer_2;
  buffer_2.Initialize(&registry_2, kCapacity, kBlockSize, kLabel);

  operation.vmo_offset = 1;
  operation.dev_offset = 1;
  builder.Add(operation, &buffer_2);

  auto requests = builder.TakeOperations();
  ASSERT_EQ(2, requests.size());
  EXPECT_EQ(kVmoid1, requests[0].vmoid);
  EXPECT_EQ(kVmoid2, requests[1].vmoid);
  EXPECT_EQ(OperationType::kWrite, requests[0].op.type);
  EXPECT_EQ(OperationType::kWrite, requests[1].op.type);
  EXPECT_EQ(0, requests[0].op.vmo_offset);
  EXPECT_EQ(0, requests[0].op.dev_offset);
  EXPECT_EQ(1, requests[0].op.length);
  EXPECT_EQ(1, requests[1].op.vmo_offset);
  EXPECT_EQ(1, requests[1].op.dev_offset);
  EXPECT_EQ(1, requests[1].op.length);
}

TEST(BufferedOperationsBuilderTest, TwoRequestMergeOperations) {
  BufferedOperationsBuilder builder(nullptr);

  MockVmoidRegistry registry;
  VmoBuffer buffer;
  buffer.Initialize(&registry, kCapacity, kBlockSize, kLabel);

  Operation operation;
  operation.type = OperationType::kWrite;
  operation.vmo_offset = 0;
  operation.dev_offset = 0;
  operation.length = 1;
  builder.Add(operation, &buffer);

  operation.vmo_offset = 1;
  operation.dev_offset = 1;
  builder.Add(operation, &buffer);

  auto requests = builder.TakeOperations();
  ASSERT_EQ(1, requests.size());
  EXPECT_EQ(kVmoid1, requests[0].vmoid);
  EXPECT_EQ(OperationType::kWrite, requests[0].op.type);
  EXPECT_EQ(0, requests[0].op.vmo_offset);
  EXPECT_EQ(0, requests[0].op.dev_offset);
  EXPECT_EQ(2, requests[0].op.length);
}

TEST(BufferedOperationsBuilderTest, TwoRequestDifferentType) {
  BufferedOperationsBuilder builder(nullptr);

  MockVmoidRegistry registry;
  VmoBuffer buffer;
  buffer.Initialize(&registry, kCapacity, kBlockSize, kLabel);

  Operation operation;
  operation.type = OperationType::kWrite;
  operation.vmo_offset = 0;
  operation.dev_offset = 0;
  operation.length = 1;
  builder.Add(operation, &buffer);

  operation.type = OperationType::kRead;
  operation.vmo_offset = 1;
  operation.dev_offset = 1;
  builder.Add(operation, &buffer);

  auto requests = builder.TakeOperations();
  ASSERT_EQ(2, requests.size());
  EXPECT_EQ(kVmoid1, requests[0].vmoid);
  EXPECT_EQ(kVmoid1, requests[1].vmoid);
  EXPECT_EQ(OperationType::kWrite, requests[0].op.type);
  EXPECT_EQ(OperationType::kRead, requests[1].op.type);
  EXPECT_EQ(0, requests[0].op.vmo_offset);
  EXPECT_EQ(0, requests[0].op.dev_offset);
  EXPECT_EQ(1, requests[0].op.length);
  EXPECT_EQ(1, requests[1].op.vmo_offset);
  EXPECT_EQ(1, requests[1].op.dev_offset);
  EXPECT_EQ(1, requests[1].op.length);
}

TEST(BufferedOperationsBuilderTest, TwoRequestVmoGap) {
  BufferedOperationsBuilder builder(nullptr);

  MockVmoidRegistry registry;
  VmoBuffer buffer;
  buffer.Initialize(&registry, kCapacity, kBlockSize, kLabel);

  Operation operation;
  operation.type = OperationType::kWrite;
  operation.vmo_offset = 0;
  operation.dev_offset = 0;
  operation.length = 1;
  builder.Add(operation, &buffer);

  operation.vmo_offset = 2;
  operation.dev_offset = 1;
  builder.Add(operation, &buffer);

  auto requests = builder.TakeOperations();
  ASSERT_EQ(2, requests.size());
  EXPECT_EQ(kVmoid1, requests[0].vmoid);
  EXPECT_EQ(kVmoid1, requests[1].vmoid);
  EXPECT_EQ(OperationType::kWrite, requests[0].op.type);
  EXPECT_EQ(OperationType::kWrite, requests[1].op.type);
  EXPECT_EQ(0, requests[0].op.vmo_offset);
  EXPECT_EQ(0, requests[0].op.dev_offset);
  EXPECT_EQ(1, requests[0].op.length);
  EXPECT_EQ(2, requests[1].op.vmo_offset);
  EXPECT_EQ(1, requests[1].op.dev_offset);
  EXPECT_EQ(1, requests[1].op.length);
}

TEST(BufferedOperationsBuilderTest, TwoRequestDeviceGap) {
  BufferedOperationsBuilder builder(nullptr);

  MockVmoidRegistry registry;
  VmoBuffer buffer;
  buffer.Initialize(&registry, kCapacity, kBlockSize, kLabel);

  Operation operation;
  operation.type = OperationType::kWrite;
  operation.vmo_offset = 0;
  operation.dev_offset = 0;
  operation.length = 1;
  builder.Add(operation, &buffer);

  operation.vmo_offset = 1;
  operation.dev_offset = 2;
  builder.Add(operation, &buffer);

  auto requests = builder.TakeOperations();
  ASSERT_EQ(2, requests.size());
  EXPECT_EQ(kVmoid1, requests[0].vmoid);
  EXPECT_EQ(kVmoid1, requests[1].vmoid);
  EXPECT_EQ(OperationType::kWrite, requests[0].op.type);
  EXPECT_EQ(OperationType::kWrite, requests[1].op.type);
  EXPECT_EQ(0, requests[0].op.vmo_offset);
  EXPECT_EQ(0, requests[0].op.dev_offset);
  EXPECT_EQ(1, requests[0].op.length);
  EXPECT_EQ(1, requests[1].op.vmo_offset);
  EXPECT_EQ(2, requests[1].op.dev_offset);
  EXPECT_EQ(1, requests[1].op.length);
}

TEST(BufferedOperationsBuilderTest, TwoRequestReplaceOperation) {
  BufferedOperationsBuilder builder(nullptr);

  MockVmoidRegistry registry;
  VmoBuffer buffer;
  buffer.Initialize(&registry, kCapacity, kBlockSize, kLabel);

  Operation operation;
  operation.type = OperationType::kRead;
  operation.vmo_offset = 0;
  operation.dev_offset = 0;
  operation.length = 1;
  builder.Add(operation, &buffer);

  operation.length = 2;
  builder.Add(operation, &buffer);

  auto requests = builder.TakeOperations();
  ASSERT_EQ(1, requests.size());
  EXPECT_EQ(kVmoid1, requests[0].vmoid);
  EXPECT_EQ(OperationType::kRead, requests[0].op.type);
  EXPECT_EQ(0, requests[0].op.vmo_offset);
  EXPECT_EQ(0, requests[0].op.dev_offset);
  EXPECT_EQ(2, requests[0].op.length);
}

TEST(BufferedOperationsBuilderTest, TwoRequestDifferentDeviceOffset) {
  BufferedOperationsBuilder builder(nullptr);

  MockVmoidRegistry registry;
  VmoBuffer buffer;
  buffer.Initialize(&registry, kCapacity, kBlockSize, kLabel);

  Operation operation;
  operation.type = OperationType::kRead;
  operation.vmo_offset = 0;
  operation.dev_offset = 0;
  operation.length = 1;
  builder.Add(operation, &buffer);

  operation.length = 2;
  operation.dev_offset = 2;
  builder.Add(operation, &buffer);

  auto requests = builder.TakeOperations();
  ASSERT_EQ(2, requests.size());
  EXPECT_EQ(kVmoid1, requests[0].vmoid);
  EXPECT_EQ(kVmoid1, requests[1].vmoid);
  EXPECT_EQ(OperationType::kRead, requests[0].op.type);
  EXPECT_EQ(OperationType::kRead, requests[1].op.type);
  EXPECT_EQ(0, requests[0].op.vmo_offset);
  EXPECT_EQ(0, requests[0].op.dev_offset);
  EXPECT_EQ(1, requests[0].op.length);
  EXPECT_EQ(0, requests[1].op.vmo_offset);
  EXPECT_EQ(2, requests[1].op.dev_offset);
  EXPECT_EQ(2, requests[1].op.length);
}

TEST(BufferedOperationsBuilderTest, TwoRequestDifferentVmoOffset) {
  BufferedOperationsBuilder builder(nullptr);

  MockVmoidRegistry registry;
  VmoBuffer buffer;
  buffer.Initialize(&registry, kCapacity, kBlockSize, kLabel);

  Operation operation;
  operation.type = OperationType::kRead;
  operation.vmo_offset = 0;
  operation.dev_offset = 0;
  operation.length = 1;
  builder.Add(operation, &buffer);

  operation.length = 2;
  operation.vmo_offset = 2;
  builder.Add(operation, &buffer);

  auto requests = builder.TakeOperations();
  ASSERT_EQ(2, requests.size());
  EXPECT_EQ(kVmoid1, requests[0].vmoid);
  EXPECT_EQ(kVmoid1, requests[1].vmoid);
  EXPECT_EQ(OperationType::kRead, requests[0].op.type);
  EXPECT_EQ(OperationType::kRead, requests[1].op.type);
  EXPECT_EQ(0, requests[0].op.vmo_offset);
  EXPECT_EQ(0, requests[0].op.dev_offset);
  EXPECT_EQ(1, requests[0].op.length);
  EXPECT_EQ(2, requests[1].op.vmo_offset);
  EXPECT_EQ(0, requests[1].op.dev_offset);
  EXPECT_EQ(2, requests[1].op.length);
}

}  // namespace
