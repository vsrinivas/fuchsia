// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/operation/operation.h>

#include <unittest/unittest.h>

namespace {

struct TestOp {
    int dummy;
};

struct TestOpTraits {
    using OperationType = TestOp;

    static OperationType* Alloc(size_t op_size) {
        fbl::AllocChecker ac;
        fbl::unique_ptr<uint8_t[]> raw;
        if constexpr (alignof(OperationType) > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
            raw = fbl::unique_ptr<uint8_t[]>(
                new (static_cast<std::align_val_t>(alignof(OperationType)), &ac) uint8_t[op_size]);
        } else {
            raw = fbl::unique_ptr<uint8_t[]>(new (&ac) uint8_t[op_size]);
        }
        if (!ac.check()) {
            return nullptr;
        }
        return reinterpret_cast<TestOp*>(raw.release());
    }

    static void Free(OperationType* op) {
        delete[] reinterpret_cast<uint8_t*>(op);
    }
};

using TestOpCallback = void (*)(void*, zx_status_t, TestOp*);

struct CallbackTraits {
    using CallbackType = TestOpCallback;

    static std::tuple<zx_status_t> AutoCompleteArgs() {
        return std::make_tuple(ZX_ERR_INTERNAL);
    }

    static void Callback(const CallbackType* callback, void* cookie, TestOp* op,
                         zx_status_t status) {
        (*callback)(cookie, status, op);
    }
};

struct Operation : public operation::Operation<Operation, TestOpTraits, void> {
    using BaseClass = operation::Operation<Operation, TestOpTraits, void>;
    using BaseClass::BaseClass;
};

struct UnownedOperation : public operation::UnownedOperation<UnownedOperation, TestOpTraits,
                                                             CallbackTraits, void> {
    using BaseClass = operation::UnownedOperation<UnownedOperation, TestOpTraits,
                                                  CallbackTraits, void>;
    using BaseClass::BaseClass;
};

using OperationQueue = operation::OperationQueue<Operation, TestOpTraits, void>;
using UnownedOperationQueue = operation::UnownedOperationQueue<UnownedOperation, TestOpTraits,
                                                               CallbackTraits, void>;

constexpr size_t kParentOpSize = sizeof(TestOp);
constexpr size_t kOpSize = Operation::OperationSize(kParentOpSize);

bool TrivialLifetimeTest() {
    BEGIN_TEST;
    OperationQueue queue;
    UnownedOperationQueue unowned_queue;
    END_TEST;
}

bool SingleOperationTest() {
    BEGIN_TEST;
    std::optional<Operation> operation = Operation::Alloc(kOpSize);
    ASSERT_TRUE(operation.has_value());

    OperationQueue queue;
    EXPECT_TRUE(queue.pop() == std::nullopt);
    queue.push(std::move(*operation));
    EXPECT_TRUE(queue.pop() != std::nullopt);
    EXPECT_TRUE(queue.pop() == std::nullopt);
    END_TEST;
}

bool MultipleOperationTest() {
    BEGIN_TEST;
    OperationQueue queue;

    for (size_t i = 0; i < 10; i++) {
        std::optional<Operation> operation = Operation::Alloc(kOpSize);
        ASSERT_TRUE(operation.has_value());
        queue.push(std::move(*operation));
    }

    for (size_t i = 0; i < 10; i++) {
        EXPECT_TRUE(queue.pop() != std::nullopt);
    }
    EXPECT_TRUE(queue.pop() == std::nullopt);
    END_TEST;
}

bool ReleaseTest() {
    BEGIN_TEST;
    OperationQueue queue;

    for (size_t i = 0; i < 10; i++) {
        std::optional<Operation> operation = Operation::Alloc(kOpSize);
        ASSERT_TRUE(operation.has_value());
        queue.push(std::move(*operation));
    }

    queue.Release();
    EXPECT_TRUE(queue.pop() == std::nullopt);
    END_TEST;
}

bool MultipleLayerTest() {
    BEGIN_TEST;

    using FirstLayerOp = UnownedOperation;
    using SecondLayerOp = Operation;

    constexpr size_t kBaseOpSize = sizeof(TestOp);
    constexpr size_t kFirstLayerOpSize = FirstLayerOp::OperationSize(kBaseOpSize);
    constexpr size_t kSecondLayerOpSize = SecondLayerOp::OperationSize(kFirstLayerOpSize);

    OperationQueue queue;
    for (size_t i = 0; i < 10; i++) {
        std::optional<SecondLayerOp> operation = SecondLayerOp::Alloc(kSecondLayerOpSize,
                                                                      kFirstLayerOpSize);
        ASSERT_TRUE(operation.has_value());
        queue.push(std::move(*operation));
    }

    UnownedOperationQueue queue2;
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

    END_TEST;
}

bool MultipleLayerWithStorageTest() {
    BEGIN_TEST;

    struct FirstLayerOp : public operation::UnownedOperation<FirstLayerOp, TestOpTraits,
                                                             CallbackTraits, char> {

        using BaseClass = operation::UnownedOperation<FirstLayerOp, TestOpTraits,
                                                      CallbackTraits, char>;
        using BaseClass::BaseClass;
    };

    struct SecondLayerOp : public operation::Operation<SecondLayerOp, TestOpTraits, uint64_t> {
        using BaseClass = operation::Operation<SecondLayerOp, TestOpTraits, uint64_t>;
        using BaseClass::BaseClass;
    };

    constexpr size_t kBaseOpSize = sizeof(TestOp);
    constexpr size_t kFirstLayerOpSize = FirstLayerOp::OperationSize(kBaseOpSize);
    constexpr size_t kSecondLayerOpSize = SecondLayerOp::OperationSize(kFirstLayerOpSize);

    operation::OperationQueue<SecondLayerOp, TestOpTraits, uint64_t> queue;
    for (size_t i = 0; i < 10; i++) {
        std::optional<SecondLayerOp> operation = SecondLayerOp::Alloc(kSecondLayerOpSize,
                                                                      kFirstLayerOpSize);
        ASSERT_TRUE(operation.has_value());
        *operation->private_storage() = i;
        EXPECT_EQ(*operation->private_storage(), i);
        queue.push(std::move(*operation));
    }

    operation::UnownedOperationQueue<FirstLayerOp, TestOpTraits, CallbackTraits, char> queue2;
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

    END_TEST;
}

bool MultipleLayerWithCallbackTest() {
    BEGIN_TEST;

    struct FirstLayerOp : public operation::UnownedOperation<FirstLayerOp, TestOpTraits,
                                                             CallbackTraits, char> {
        using BaseClass = operation::UnownedOperation<FirstLayerOp, TestOpTraits,
                                                      CallbackTraits, char>;
        using BaseClass::BaseClass;
    };

    struct SecondLayerOp : public operation::Operation<SecondLayerOp, TestOpTraits, uint64_t> {
        using BaseClass = operation::Operation<SecondLayerOp, TestOpTraits, uint64_t>;
        using BaseClass::BaseClass;
    };

    constexpr size_t kBaseOpSize = sizeof(TestOp);
    constexpr size_t kFirstLayerOpSize = FirstLayerOp::OperationSize(kBaseOpSize);
    constexpr size_t kSecondLayerOpSize = SecondLayerOp::OperationSize(kFirstLayerOpSize);

    operation::OperationQueue<SecondLayerOp, TestOpTraits, uint64_t> queue;
    for (size_t i = 0; i < 10; i++) {
        std::optional<SecondLayerOp> operation = SecondLayerOp::Alloc(kSecondLayerOpSize,
                                                                      kFirstLayerOpSize);
        ASSERT_TRUE(operation.has_value());
        *operation->private_storage() = i;
        EXPECT_EQ(*operation->private_storage(), i);
        queue.push(std::move(*operation));
    }

    auto callback = [](void* ctx, zx_status_t status, TestOp* operation) {
        auto* queue = static_cast<operation::OperationQueue<SecondLayerOp, TestOpTraits, uint64_t>*>(ctx);
        queue->push(SecondLayerOp(operation, kFirstLayerOpSize));
    };
    TestOpCallback cb = callback;

    {
        operation::UnownedOperationQueue<FirstLayerOp, TestOpTraits, CallbackTraits, char> queue2;
        for (auto operation = queue.pop(); operation; operation = queue.pop()) {
            FirstLayerOp unowned(operation->take(), &cb, &queue, kBaseOpSize);
            queue2.push(std::move(unowned));
        }
    }

    size_t count = 0;
    for (auto operation = queue.pop(); operation; operation = queue.pop()) {
        EXPECT_EQ(*operation->private_storage(), count);
        ++count;
    }
    EXPECT_EQ(count, 10);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(OperationQueueTests)
RUN_TEST_SMALL(TrivialLifetimeTest)
RUN_TEST_SMALL(SingleOperationTest)
RUN_TEST_SMALL(MultipleOperationTest)
RUN_TEST_SMALL(ReleaseTest)
RUN_TEST_SMALL(MultipleLayerTest)
RUN_TEST_SMALL(MultipleLayerWithStorageTest)
RUN_TEST_SMALL(MultipleLayerWithCallbackTest)
END_TEST_CASE(OperationQueueTests);
