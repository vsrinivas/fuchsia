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

using OperationPool = operation::OperationPool<Operation, TestOpTraits, void>;

constexpr size_t kParentOpSize = sizeof(TestOp);
constexpr size_t kOpSize = Operation::OperationSize(kParentOpSize);

bool TrivialLifetimeTest() {
    BEGIN_TEST;
    OperationPool pool;
    END_TEST;
}

bool SingleOperationTest() {
    BEGIN_TEST;
    std::optional<Operation> operation = Operation::Alloc(kOpSize);
    ASSERT_TRUE(operation.has_value());

    OperationPool pool;
    EXPECT_TRUE(pool.pop() == std::nullopt);
    pool.push(std::move(*operation));
    EXPECT_TRUE(pool.pop() != std::nullopt);
    EXPECT_TRUE(pool.pop() == std::nullopt);
    END_TEST;
}

bool MultipleOperationTest() {
    BEGIN_TEST;
    OperationPool pool;

    for (size_t i = 0; i < 10; i++) {
        std::optional<Operation> operation = Operation::Alloc(kOpSize);
        ASSERT_TRUE(operation.has_value());
        pool.push(std::move(*operation));
    }

    for (size_t i = 0; i < 10; i++) {
        EXPECT_TRUE(pool.pop() != std::nullopt);
    }
    EXPECT_TRUE(pool.pop() == std::nullopt);
    END_TEST;
}

bool ReleaseTest() {
    BEGIN_TEST;
    OperationPool pool;

    for (size_t i = 0; i < 10; i++) {
        std::optional<Operation> operation = Operation::Alloc(kOpSize);
        ASSERT_TRUE(operation.has_value());
        pool.push(std::move(*operation));
    }

    pool.Release();
    EXPECT_TRUE(pool.pop() == std::nullopt);
    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(OperationPoolTests)
RUN_TEST_SMALL(TrivialLifetimeTest)
RUN_TEST_SMALL(SingleOperationTest)
RUN_TEST_SMALL(MultipleOperationTest)
RUN_TEST_SMALL(ReleaseTest)
END_TEST_CASE(OperationPoolTests)
