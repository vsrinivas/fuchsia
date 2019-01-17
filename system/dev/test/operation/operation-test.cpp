// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/operation/operation.h>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
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

constexpr size_t kParentOpSize = sizeof(TestOp);
constexpr size_t kOpSize = Operation::OperationSize(kParentOpSize);

bool AllocTest() {
    BEGIN_TEST;
    std::optional<Operation> op = Operation::Alloc(kOpSize);
    EXPECT_TRUE(op.has_value());
    END_TEST;
}

bool PrivateStorageTest() {
    BEGIN_TEST;

    struct Private : public operation::Operation<Private, TestOpTraits, uint32_t> {
          using BaseClass = operation::Operation<Private, TestOpTraits, uint32_t>;
          using BaseClass::BaseClass;
    };

    constexpr size_t kOperationSize = Private::OperationSize(kParentOpSize);
    auto operation = Private::Alloc(kOperationSize);
    ASSERT_TRUE(operation.has_value());
    *operation->private_storage() = 1001;
    ASSERT_EQ(*operation->private_storage(), 1001);
    END_TEST;
}

bool MultipleSectionTest() {
    BEGIN_TEST;

    constexpr size_t kBaseOpSize = sizeof(TestOp);
    constexpr size_t kFirstLayerOpSize = Operation::OperationSize(kBaseOpSize);
    constexpr size_t kSecondLayerOpSize =
        UnownedOperation::OperationSize(kFirstLayerOpSize);
    constexpr size_t kThirdLayerOpSize =
        UnownedOperation::OperationSize(kSecondLayerOpSize);

    std::optional<Operation> operation = Operation::Alloc(kThirdLayerOpSize);
    ASSERT_TRUE(operation.has_value());

    UnownedOperation operation2(operation->take(), nullptr, nullptr, kFirstLayerOpSize);
    UnownedOperation operation3(operation2.take(), nullptr, nullptr, kSecondLayerOpSize);
    operation = Operation(operation3.take(), kBaseOpSize);

    END_TEST;
}

bool CallbackTest() {
    BEGIN_TEST;
    constexpr size_t kBaseOpSize = sizeof(TestOp);
    constexpr size_t kFirstLayerOpSize = Operation::OperationSize(kBaseOpSize);
    constexpr size_t kSecondLayerOpSize =
        UnownedOperation::OperationSize(kFirstLayerOpSize);

    bool called = false;
    auto callback = [](void* ctx, zx_status_t st, TestOp* operation) -> void {
        *static_cast<bool*>(ctx) = true;
        // We take ownership.
        Operation unused(operation, kBaseOpSize);
    };
    TestOpCallback cb = callback;
    std::optional<Operation> operation = Operation::Alloc(kSecondLayerOpSize);
    ASSERT_TRUE(operation.has_value());

    UnownedOperation operation2(operation->take(), &cb, &called, kFirstLayerOpSize);
    operation2.Complete(ZX_OK);
    EXPECT_TRUE(called);

    END_TEST;
}

bool AutoCallbackTest() {
    BEGIN_TEST;
    constexpr size_t kBaseOpSize = sizeof(TestOp);
    constexpr size_t kFirstLayerOpSize = Operation::OperationSize(kBaseOpSize);
    constexpr size_t kSecondLayerOpSize =
        UnownedOperation::OperationSize(kFirstLayerOpSize);

    bool called = false;
    auto callback = [](void* ctx, zx_status_t st, TestOp* operation) {
        *static_cast<bool*>(ctx) = true;
        // We take ownership.
        Operation unused(operation, kBaseOpSize);
    };
    TestOpCallback cb = callback;

    std::optional<Operation> operation = Operation::Alloc(kSecondLayerOpSize);
    ASSERT_TRUE(operation.has_value());

    {
        UnownedOperation operation2(operation->take(), &cb, &called, kFirstLayerOpSize);
    }
    EXPECT_TRUE(called);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(OperationTests)
RUN_TEST_SMALL(AllocTest)
RUN_TEST_SMALL(PrivateStorageTest)
RUN_TEST_SMALL(MultipleSectionTest)
RUN_TEST_SMALL(CallbackTest)
RUN_TEST_SMALL(AutoCallbackTest)
END_TEST_CASE(OperationTests);
