// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/operation/operation.h>

#include <memory>

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

using OperationList = operation::OperationList<Operation, TestOpTraits, void>;
using BorrowedOperationList =
    operation::BorrowedOperationList<BorrowedOperation, TestOpTraits, CallbackTraits, void>;

constexpr size_t kParentOpSize = sizeof(TestOp);

TEST(OperationListTest, TrivialLifetime) {
  OperationList list;
  BorrowedOperationList unowned_list;
}

TEST(OperationListTest, Move) {
  OperationList list;

  std::optional<Operation> opt_operation = Operation::Alloc(kParentOpSize);
  ASSERT_TRUE(opt_operation.has_value());
  Operation operation = *std::move(opt_operation);
  list.push_back(&operation);
  EXPECT_EQ(list.size(), 1u);

  OperationList list2(std::move(list));
  EXPECT_EQ(list2.size(), 1u);
  EXPECT_EQ(list.size(), 0u);
}

TEST(OperationListTest, SingleOperation) {
  std::optional<Operation> opt_operation = Operation::Alloc(kParentOpSize);
  ASSERT_TRUE(opt_operation.has_value());
  Operation operation = *std::move(opt_operation);

  OperationList list;
  // Empty list.
  EXPECT_TRUE(list.find(&operation) == std::nullopt);
  EXPECT_EQ(list.size(), 0u);

  list.push_back(&operation);
  EXPECT_EQ(list.size(), 1u);

  // List only has one operation.
  EXPECT_TRUE(list.prev(&operation) == std::nullopt);
  EXPECT_TRUE(list.next(&operation) == std::nullopt);

  std::optional<size_t> idx = list.find(&operation);
  EXPECT_TRUE(idx.has_value());
  EXPECT_EQ(idx.value(), 0u);

  // Delete the operation and verify it's no longer in the list.
  EXPECT_TRUE(list.erase(&operation));
  EXPECT_EQ(list.size(), 0u);

  idx = list.find(&operation);
  EXPECT_FALSE(idx.has_value());
}

TEST(OperationListTest, MultipleOperation) {
  OperationList list;
  // This is for verifying prev / next pointer values when iterating the list.
  TestOp* ops[10];

  for (size_t i = 0; i < 10; i++) {
    std::optional<Operation> opt_operation = Operation::Alloc(kParentOpSize);
    ASSERT_TRUE(opt_operation.has_value());
    Operation operation = *std::move(opt_operation);

    list.push_back(&operation);
    EXPECT_EQ(list.size(), i + 1);

    ops[i] = operation.take();
  }
  EXPECT_EQ(list.size(), 10u);

  // Verify iterating in both directions.
  auto opt_operation = list.begin();
  for (size_t i = 0; i < 10; i++) {
    EXPECT_TRUE(opt_operation.has_value());
    Operation operation = *std::move(opt_operation);

    std::optional<size_t> idx = list.find(&operation);
    EXPECT_TRUE(idx.has_value());
    EXPECT_EQ(idx.value(), i);

    auto prev = list.prev(&operation);
    if (i == 0) {
      EXPECT_FALSE(prev.has_value());
    } else {
      EXPECT_TRUE(prev.has_value());
      EXPECT_EQ(prev->operation(), ops[i - 1]);
    }

    auto next = list.next(&operation);
    if (i == 9) {
      EXPECT_FALSE(next.has_value());
    } else {
      EXPECT_TRUE(next.has_value());
      EXPECT_EQ(next->operation(), ops[i + 1]);
    }

    opt_operation = std::move(next);
  }
  EXPECT_FALSE(opt_operation.has_value());

  for (size_t i = 0; i < 10; i++) {
    auto opt_operation = list.begin();
    EXPECT_TRUE(opt_operation.has_value());
    Operation operation = *std::move(opt_operation);
    EXPECT_TRUE(list.erase(&operation));

    // Force the destructor to run.
    __UNUSED auto op = Operation(ops[i], kParentOpSize);
  }
  EXPECT_EQ(list.size(), 0u);
  EXPECT_FALSE(list.begin().has_value());
}

TEST(OperationListTest, Release) {
  OperationList list;
  TestOp* ops[10];

  for (size_t i = 0; i < 10; i++) {
    std::optional<Operation> opt_operation = Operation::Alloc(kParentOpSize);
    ASSERT_TRUE(opt_operation.has_value());
    Operation operation = *std::move(opt_operation);
    list.push_back(&operation);
    EXPECT_EQ(list.size(), i + 1);

    ops[i] = operation.take();
  }

  list.Release();
  EXPECT_EQ(list.size(), 0u);
  EXPECT_FALSE(list.begin().has_value());

  for (size_t i = 0; i < 10; i++) {
    // Force the destructor to run.
    __UNUSED auto op = Operation(ops[i], kParentOpSize);
  }
}

TEST(OperationListTest, MultipleLayer) {
  using FirstLayerOp = BorrowedOperation;
  using SecondLayerOp = Operation;

  constexpr size_t kBaseOpSize = sizeof(TestOp);
  constexpr size_t kFirstLayerOpSize = FirstLayerOp::OperationSize(kBaseOpSize);

  TestOp* ops[10];

  OperationList second_layer_list;
  for (size_t i = 0; i < 10; i++) {
    std::optional<SecondLayerOp> opt_operation = SecondLayerOp::Alloc(kFirstLayerOpSize);
    ASSERT_TRUE(opt_operation.has_value());
    Operation operation = *std::move(opt_operation);
    second_layer_list.push_back(&operation);
    ops[i] = operation.take();
  }
  EXPECT_EQ(second_layer_list.size(), 10u);

  BorrowedOperationList first_layer_list;
  // Add the operations also into the first layer list.
  for (size_t i = 0; i < 10; i++) {
    FirstLayerOp unowned(ops[i], nullptr, nullptr, kBaseOpSize, /* allow_destruct */ false);
    first_layer_list.push_back(&unowned);
  }
  EXPECT_EQ(first_layer_list.size(), 10u);

  // Remove the operations from both lists.
  for (size_t i = 0; i < 10; i++) {
    FirstLayerOp unowned(ops[i], kBaseOpSize);
    std::optional<size_t> idx = first_layer_list.find(&unowned);
    EXPECT_TRUE(idx.has_value());
    EXPECT_EQ(idx.value(), 0u);
    EXPECT_TRUE(first_layer_list.erase(&unowned));

    SecondLayerOp operation(unowned.take(), kFirstLayerOpSize);
    idx = second_layer_list.find(&operation);
    EXPECT_TRUE(idx.has_value());
    EXPECT_EQ(idx.value(), 0u);
    EXPECT_TRUE(second_layer_list.erase(&operation));
  }
  EXPECT_EQ(first_layer_list.size(), 0u);
  EXPECT_EQ(second_layer_list.size(), 0u);
}

TEST(OperationListTest, MultipleLayerWithStorage) {
  struct FirstLayerOp
      : public operation::BorrowedOperation<FirstLayerOp, TestOpTraits, CallbackTraits, char> {
    using BaseClass =
        operation::BorrowedOperation<FirstLayerOp, TestOpTraits, CallbackTraits, char>;
    using BaseClass::BaseClass;
  };

  struct SecondLayerOp : public operation::Operation<SecondLayerOp, TestOpTraits, uint64_t> {
    using BaseClass = operation::Operation<SecondLayerOp, TestOpTraits, uint64_t>;
    using BaseClass::BaseClass;
  };

  constexpr size_t kBaseOpSize = sizeof(TestOp);
  constexpr size_t kFirstLayerOpSize = FirstLayerOp::OperationSize(kBaseOpSize);

  TestOp* ops[10];

  operation::OperationList<SecondLayerOp, TestOpTraits, uint64_t> second_layer_list;
  for (size_t i = 0; i < 10; i++) {
    std::optional<SecondLayerOp> opt_operation = SecondLayerOp::Alloc(kFirstLayerOpSize);
    ASSERT_TRUE(opt_operation.has_value());
    auto operation = *std::move(opt_operation);

    *operation.private_storage() = i;
    EXPECT_EQ(*operation.private_storage(), i);
    second_layer_list.push_back(&operation);
    ops[i] = operation.take();
  }
  EXPECT_EQ(second_layer_list.size(), 10u);

  operation::BorrowedOperationList<FirstLayerOp, TestOpTraits, CallbackTraits, char>
      first_layer_list;
  // Add the operations also into the first layer list.
  for (size_t i = 0; i < 10; i++) {
    FirstLayerOp unowned(ops[i], nullptr, nullptr, kBaseOpSize, /* allow_destruct */ false);
    *unowned.private_storage() = static_cast<char>('a' + first_layer_list.size());
    first_layer_list.push_back(&unowned);
  }
  EXPECT_EQ(first_layer_list.size(), 10u);

  // Verify the first layer list node's private storage and also erase them along the way.
  size_t count = 0;
  auto opt_unowned = first_layer_list.begin();
  while (opt_unowned) {
    auto unowned = *std::move(opt_unowned);
    auto next = first_layer_list.next(&unowned);

    EXPECT_EQ(*unowned.private_storage(), static_cast<char>('a' + count));
    EXPECT_TRUE(first_layer_list.erase(&unowned));

    ++count;
    opt_unowned = std::move(next);
  }
  EXPECT_EQ(count, 10);
  EXPECT_EQ(first_layer_list.size(), 0u);

  // Verify the second layer list node's private storage and also erase them along the way.
  count = 0;
  auto opt_operation = second_layer_list.begin();
  while (opt_operation) {
    auto operation = *std::move(opt_operation);
    auto next = second_layer_list.next(&operation);

    EXPECT_EQ(*operation.private_storage(), count);
    EXPECT_TRUE(second_layer_list.erase(&operation));

    ++count;
    opt_operation = std::move(next);
  }
  EXPECT_EQ(count, 10);
  EXPECT_EQ(second_layer_list.size(), 0u);

  for (size_t i = 0; i < 10; i++) {
    // Force the destructor to run.
    __UNUSED auto op = Operation(ops[i], kParentOpSize);
  }
}

TEST(OperationListTest, MultipleLayerWithCallback) {
  struct FirstLayerOp
      : public operation::BorrowedOperation<FirstLayerOp, TestOpTraits, CallbackTraits, char> {
    using BaseClass =
        operation::BorrowedOperation<FirstLayerOp, TestOpTraits, CallbackTraits, char>;
    using BaseClass::BaseClass;
  };

  struct SecondLayerOp : public operation::Operation<SecondLayerOp, TestOpTraits, uint64_t> {
    using BaseClass = operation::Operation<SecondLayerOp, TestOpTraits, uint64_t>;
    using BaseClass::BaseClass;
  };
  constexpr size_t kBaseOpSize = sizeof(TestOp);
  constexpr size_t kFirstLayerOpSize = FirstLayerOp::OperationSize(kBaseOpSize);

  TestOp* ops[10];

  operation::OperationList<SecondLayerOp, TestOpTraits, uint64_t> second_layer_list;
  for (size_t i = 0; i < 10; i++) {
    std::optional<SecondLayerOp> opt_operation = SecondLayerOp::Alloc(kFirstLayerOpSize);
    ASSERT_TRUE(opt_operation.has_value());
    SecondLayerOp operation = *std::move(opt_operation);

    *operation.private_storage() = i;
    EXPECT_EQ(*operation.private_storage(), i);
    second_layer_list.push_back(&operation);

    ops[i] = operation.take();
  }
  EXPECT_EQ(second_layer_list.size(), 10u);

  std::atomic<size_t> num_callbacks{0};

  auto callback = [](void* ctx, zx_status_t status, TestOp* operation) {
    auto counter = static_cast<std::atomic<size_t>*>(ctx);
    ++(*counter);
  };

  TestOpCallback cb = callback;

  {
    operation::BorrowedOperationList<FirstLayerOp, TestOpTraits, CallbackTraits, char>
        first_layer_list;

    // Store the operations into the first layer list.
    for (size_t i = 0; i < 10; i++) {
      FirstLayerOp unowned(ops[i], &cb, &num_callbacks, kBaseOpSize,
                           /* allow_destruct */ false);
      first_layer_list.push_back(&unowned);
    }
    EXPECT_EQ(first_layer_list.size(), 10u);
    EXPECT_EQ(second_layer_list.size(), 10u);
  }
  // The first layer list destruction should not trigger any callbacks.
  EXPECT_EQ(num_callbacks.load(), 0u);

  second_layer_list.Release();
  EXPECT_EQ(second_layer_list.size(), 0u);

  for (int i = 0; i < 10; i++) {
    // Force the destructor to run.
    __UNUSED auto op = SecondLayerOp(ops[i], kFirstLayerOpSize);
  }
}

}  // namespace
