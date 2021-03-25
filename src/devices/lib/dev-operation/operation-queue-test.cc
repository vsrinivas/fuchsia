// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/operation/helpers/alloc_checker.h>
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
    operation::AllocChecker ac;
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

using OperationQueue = operation::OperationQueue<Operation, TestOpTraits, void>;
using BorrowedOperationQueue =
    operation::BorrowedOperationQueue<BorrowedOperation, TestOpTraits, CallbackTraits, void>;

constexpr size_t kParentOpSize = sizeof(TestOp);

TEST(OperationQueueTest, TrivialLifetime) {
  OperationQueue queue;
  BorrowedOperationQueue unowned_queue;
}

TEST(OperationQueueTest, SingleOperation) {
  std::optional<Operation> operation = Operation::Alloc(kParentOpSize);
  ASSERT_TRUE(operation.has_value());

  OperationQueue queue;
  EXPECT_TRUE(queue.pop() == std::nullopt);
  queue.push(*std::move(operation));
  EXPECT_TRUE(queue.pop() != std::nullopt);
  EXPECT_TRUE(queue.pop() == std::nullopt);
}

TEST(OperationQueueTest, MultipleOperation) {
  OperationQueue queue;

  for (size_t i = 0; i < 10; i++) {
    std::optional<Operation> operation = Operation::Alloc(kParentOpSize);
    ASSERT_TRUE(operation.has_value());
    queue.push(*std::move(operation));
  }

  for (size_t i = 0; i < 10; i++) {
    EXPECT_TRUE(queue.pop() != std::nullopt);
  }
  EXPECT_TRUE(queue.pop() == std::nullopt);
}

TEST(OperationQueueTest, Erase) {
  OperationQueue queue;
  TestOpTraits::OperationType* target_ptr = nullptr;
  for (size_t i = 0; i < 10; i++) {
    std::optional<Operation> operation = Operation::Alloc(kParentOpSize);
    if (i == 5) {
      target_ptr = operation->operation();
    }
    ASSERT_TRUE(operation.has_value());
    queue.push(*std::move(operation));
  }
  Operation tmp(target_ptr, kParentOpSize, true);
  EXPECT_TRUE(queue.erase(&tmp));
  for (size_t i = 0; i < 10; i++) {
    auto val = queue.pop();
    EXPECT_NE(val->operation(), target_ptr);
    if (i == 9) {
      EXPECT_TRUE(val == std::nullopt);
    } else {
      EXPECT_TRUE(val != std::nullopt);
    }
  }
  EXPECT_TRUE(queue.pop() == std::nullopt);
}

TEST(OperationQueueTest, Release) {
  OperationQueue queue;

  for (size_t i = 0; i < 10; i++) {
    std::optional<Operation> operation = Operation::Alloc(kParentOpSize);
    ASSERT_TRUE(operation.has_value());
    queue.push(*std::move(operation));
  }

  queue.Release();
  EXPECT_TRUE(queue.pop() == std::nullopt);
}

TEST(OperationQueueTest, MultipleLayer) {
  using FirstLayerOp = BorrowedOperation;
  using SecondLayerOp = Operation;

  constexpr size_t kBaseOpSize = sizeof(TestOp);
  constexpr size_t kFirstLayerOpSize = FirstLayerOp::OperationSize(kBaseOpSize);

  OperationQueue queue;
  for (size_t i = 0; i < 10; i++) {
    std::optional<SecondLayerOp> operation = SecondLayerOp::Alloc(kFirstLayerOpSize);
    ASSERT_TRUE(operation.has_value());
    queue.push(*std::move(operation));
  }

  BorrowedOperationQueue queue2;
  size_t count = 0;
  for (auto operation = queue.pop(); operation; operation = queue.pop()) {
    FirstLayerOp unowned(operation->take(), nullptr, nullptr, kBaseOpSize);
    queue2.push(std::move(unowned));
    ++count;
  }
  EXPECT_EQ(count, 10);

  count = 0;
  for (auto unowned = queue2.pop(); unowned; unowned = queue2.pop()) {
    SecondLayerOp operation(unowned->take(), kFirstLayerOpSize);
    queue.push(std::move(operation));
    ++count;
  }
  EXPECT_EQ(count, 10);
}

TEST(OperationQueueTest, MultipleLayerWithStorage) {
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

  operation::OperationQueue<SecondLayerOp, TestOpTraits, uint64_t> queue;
  for (size_t i = 0; i < 10; i++) {
    std::optional<SecondLayerOp> operation = SecondLayerOp::Alloc(kFirstLayerOpSize);
    ASSERT_TRUE(operation.has_value());
    *operation->private_storage() = i;
    EXPECT_EQ(*operation->private_storage(), i);
    queue.push(*std::move(operation));
  }

  operation::BorrowedOperationQueue<FirstLayerOp, TestOpTraits, CallbackTraits, char> queue2;
  size_t count = 0;
  for (auto operation = queue.pop(); operation; operation = queue.pop()) {
    FirstLayerOp unowned(operation->take(), nullptr, nullptr, kBaseOpSize);
    *unowned.private_storage() = static_cast<char>('a' + count);
    queue2.push(std::move(unowned));
    ++count;
  }
  EXPECT_EQ(count, 10);

  count = 0;
  for (auto unowned = queue2.pop(); unowned; unowned = queue2.pop()) {
    EXPECT_EQ(*unowned->private_storage(), static_cast<char>('a' + count));
    SecondLayerOp operation(unowned->take(), kFirstLayerOpSize);
    EXPECT_EQ(*operation.private_storage(), count);
    queue.push(std::move(operation));
    ++count;
  }
  EXPECT_EQ(count, 10);
}

TEST(OperationQueueTest, MultipleLayerWithCallback) {
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

  operation::OperationQueue<SecondLayerOp, TestOpTraits, uint64_t> queue;
  for (size_t i = 0; i < 10; i++) {
    std::optional<SecondLayerOp> operation = SecondLayerOp::Alloc(kFirstLayerOpSize);
    ASSERT_TRUE(operation.has_value());
    *operation->private_storage() = i;
    EXPECT_EQ(*operation->private_storage(), i);
    queue.push(*std::move(operation));
  }

  auto callback = [](void* ctx, zx_status_t status, TestOp* operation) {
    auto* queue =
        static_cast<operation::OperationQueue<SecondLayerOp, TestOpTraits, uint64_t>*>(ctx);
    queue->push(SecondLayerOp(operation, kFirstLayerOpSize));
  };
  TestOpCallback cb = callback;

  operation::BorrowedOperationQueue<FirstLayerOp, TestOpTraits, CallbackTraits, char> queue2;
  for (auto operation = queue.pop(); operation; operation = queue.pop()) {
    FirstLayerOp unowned(operation->take(), &cb, &queue, kBaseOpSize);
    queue2.push(std::move(unowned));
  }
  queue2.CompleteAll(ZX_OK);

  size_t count = 0;
  for (auto operation = queue.pop(); operation; operation = queue.pop()) {
    EXPECT_EQ(*operation->private_storage(), count);
    ++count;
  }
  EXPECT_EQ(count, 10);
}

TEST(OperationQueueTest, ReverseQueue) {
  struct FirstLayerOp : public operation::Operation<FirstLayerOp, TestOpTraits, int> {
    using BaseClass = operation::Operation<FirstLayerOp, TestOpTraits, int>;
    using BaseClass::BaseClass;
  };

  constexpr size_t kBaseOpSize = sizeof(TestOp);

  operation::OperationQueue<FirstLayerOp, TestOpTraits, int> queue;
  for (int i = 0; i < 10; i++) {
    std::optional<FirstLayerOp> operation = FirstLayerOp::Alloc(kBaseOpSize);
    ASSERT_TRUE(operation.has_value());
    *operation->private_storage() = i;
    EXPECT_EQ(*operation->private_storage(), i);
    queue.push(*std::move(operation));
  }

  operation::OperationQueue<FirstLayerOp, TestOpTraits, int> reverse_queue;
  int i = 9;
  for (auto operation = queue.pop_last(); operation; operation = queue.pop_last()) {
    EXPECT_EQ(*operation->private_storage(), i);
    reverse_queue.push_next(std::move(*operation));
    --i;
  }
  EXPECT_EQ(i, -1);

  i = 0;
  for (auto operation = reverse_queue.pop(); operation; operation = reverse_queue.pop()) {
    EXPECT_EQ(*operation->private_storage(), i);
    ++i;
  }
  EXPECT_EQ(i, 10);
}

}  // namespace
