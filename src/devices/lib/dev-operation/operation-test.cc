// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/operation/operation.h>

#include <memory>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

namespace {

struct TestOp {
  int dummy;
};

struct TestOpTraits {
  using OperationType = TestOp;

  static OperationType* Alloc(size_t op_size) {
    fbl::AllocChecker ac;
    std::unique_ptr<uint8_t[]> raw;
    if constexpr (alignof(OperationType) > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
      raw = std::unique_ptr<uint8_t[]>(
          new (static_cast<std::align_val_t>(alignof(OperationType)), &ac) uint8_t[op_size]);
    } else {
      raw = std::unique_ptr<uint8_t[]>(new (&ac) uint8_t[op_size]);
    }
    if (!ac.check()) {
      return nullptr;
    }
    return reinterpret_cast<TestOp*>(raw.release());
  }

  static void Free(OperationType* op) { delete[] reinterpret_cast<uint8_t*>(op); }
};

using TestOpCallback = void (*)(void*, zx_status_t, TestOp*);

struct CallbackTraits {
  using CallbackType = TestOpCallback;

  static void Callback(const CallbackType* callback, void* cookie, TestOp* op, zx_status_t status) {
    (*callback)(cookie, status, op);
  }
};

struct Operation : public operation::Operation<Operation, TestOpTraits, void> {
  using BaseClass = operation::Operation<Operation, TestOpTraits, void>;
  using BaseClass::BaseClass;
};

struct BorrowedOperation
    : public operation::BorrowedOperation<BorrowedOperation, TestOpTraits, CallbackTraits, void> {
  using BaseClass =
      operation::BorrowedOperation<BorrowedOperation, TestOpTraits, CallbackTraits, void>;
  using BaseClass::BaseClass;
};

constexpr size_t kParentOpSize = sizeof(TestOp);

TEST(OperationTest, Alloc) {
  std::optional<Operation> op = Operation::Alloc(kParentOpSize);
  EXPECT_TRUE(op.has_value());
}

TEST(OperationTest, PrivateStorage) {
  struct Private : public operation::Operation<Private, TestOpTraits, uint32_t> {
    using BaseClass = operation::Operation<Private, TestOpTraits, uint32_t>;
    using BaseClass::BaseClass;
  };

  auto operation = Private::Alloc(kParentOpSize);
  ASSERT_TRUE(operation.has_value());
  *operation->private_storage() = 1001;
  ASSERT_EQ(*operation->private_storage(), 1001);
}

TEST(OperationTest, MultipleSection) {
  constexpr size_t kBaseOpSize = sizeof(TestOp);
  constexpr size_t kFirstLayerOpSize = Operation::OperationSize(kBaseOpSize);
  constexpr size_t kSecondLayerOpSize = BorrowedOperation::OperationSize(kFirstLayerOpSize);

  std::optional<Operation> operation = Operation::Alloc(kSecondLayerOpSize);
  ASSERT_TRUE(operation.has_value());

  BorrowedOperation operation2(operation->take(), nullptr, nullptr, kFirstLayerOpSize);
  BorrowedOperation operation3(operation2.take(), nullptr, nullptr, kBaseOpSize);
  operation = Operation(operation3.take(), kSecondLayerOpSize);
}

// TODO(51401): Test is disabled because it treats uninitialized memory as an
// fbl::DoublyLinkedListNode.  See fxb/51401 for details.
TEST(OperationTest, DISABLED_Callback) {
  constexpr size_t kBaseOpSize = sizeof(TestOp);
  constexpr size_t kFirstLayerOpSize = Operation::OperationSize(kBaseOpSize);

  bool called = false;
  auto callback = [](void* ctx, zx_status_t st, TestOp* operation) -> void {
    *static_cast<bool*>(ctx) = true;
    // We take ownership.
    Operation unused(operation, kFirstLayerOpSize);
  };
  TestOpCallback cb = callback;
  std::optional<Operation> operation = Operation::Alloc(kFirstLayerOpSize);
  ASSERT_TRUE(operation.has_value());

  BorrowedOperation operation2(operation->take(), &cb, &called, kBaseOpSize);
  operation2.Complete(ZX_OK);
  EXPECT_TRUE(called);
}

}  // namespace
