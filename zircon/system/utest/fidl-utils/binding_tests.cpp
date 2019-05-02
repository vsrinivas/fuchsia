// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <type_traits>

#include <fidl/test/fidlutils/c/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <unittest/unittest.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

namespace {
class BaseClass {
public:
    zx_status_t SimpleFnWithReply(uint64_t, fidl_txn_t*) { return ZX_OK; }
    zx_status_t SimpleFnWithoutReply(uint64_t) { return ZX_OK; }
    zx_status_t ConstFnWithReply(uint64_t, fidl_txn_t*) const { return ZX_OK; }
    zx_status_t ConstFnWithoutReply(uint64_t) const { return ZX_OK; }
    zx_status_t VolatileFnWithReply(uint64_t, fidl_txn_t*) volatile { return ZX_OK; }
    zx_status_t VolatileFnWithoutReply(uint64_t) volatile { return ZX_OK; }

    zx_status_t OverloadedFnWithReply(uint64_t, fidl_txn_t*) { return ZX_OK; }

    // This should not be called
    zx_status_t OverloadedFnWithReply(uint64_t) { return ZX_ERR_INTERNAL; }

    zx_status_t OverloadedFnWithoutReply(uint64_t) { return ZX_OK; }

    // This should not be called
    zx_status_t OverloadedFnWithoutReply(uint64_t, fidl_txn_t*) { return ZX_ERR_INTERNAL; }

    virtual zx_status_t VirtualFnWithReply(uint64_t magic_number, fidl_txn_t*) {
        BEGIN_HELPER;
        EXPECT_EQ(magic_number, MAGIC_NUMBER);
        return ZX_OK;
        END_HELPER;
    }

    virtual zx_status_t VirtualFnWithoutReply(uint64_t magic_number) {
        BEGIN_HELPER;
        EXPECT_EQ(magic_number, MAGIC_NUMBER);
        return ZX_OK;
        END_HELPER;
    }

    constexpr static uint64_t MAGIC_NUMBER = 42;
};

class DerivedClass : BaseClass {
public:
    virtual zx_status_t VirtualFnWithReply(uint64_t magic_number, fidl_txn_t*) override {
        BEGIN_HELPER;
        EXPECT_EQ(magic_number, MAGIC_NUMBER);
        return ZX_OK;
        END_HELPER;
    }

    virtual zx_status_t VirtualFnWithoutReply(uint64_t magic_number) override {
        BEGIN_HELPER;
        EXPECT_EQ(magic_number, MAGIC_NUMBER);
        return ZX_OK;
        END_HELPER;
    }

    constexpr static uint64_t MAGIC_NUMBER = 183;
};

using BaseBinder = fidl::Binder<BaseClass>;
using DerivedBinder = fidl::Binder<DerivedClass>;

// Compile-time checks
static_assert(std::is_same<
                  decltype(fidl_test_fidlutils_BindingTestsProtocol_ops_t::FunctionWithReply),
                  zx_status_t (*)(void*, uint64_t, fidl_txn_t*)>::value,
              "Unexpected signature for C binding of BindingTestsProtocol::FunctionWithReply");
static_assert(std::is_same<
                  decltype(fidl_test_fidlutils_BindingTestsProtocol_ops_t::FunctionWithoutReply),
                  zx_status_t (*)(void*, uint64_t)>::value,
              "Unexpected signature for C binding of BindingTestsProtocol::FunctionWithoutReply");

bool SimpleBindMemberTest() {
    BEGIN_TEST;

    BaseClass base;
    void* base_ctx = &base;
    fidl_txn_t dummy_txn;

    fidl_test_fidlutils_BindingTestsProtocol_ops_t ops = {
        .FunctionWithReply = BaseBinder::BindMember<&BaseClass::SimpleFnWithReply>,
        .FunctionWithoutReply = BaseBinder::BindMember<&BaseClass::SimpleFnWithoutReply>,
    };

    EXPECT_EQ(ops.FunctionWithReply(base_ctx, BaseClass::MAGIC_NUMBER, &dummy_txn), ZX_OK);
    EXPECT_EQ(ops.FunctionWithoutReply(base_ctx, BaseClass::MAGIC_NUMBER), ZX_OK);

    END_TEST;
}

bool ConstBindMemberTest() {
    BEGIN_TEST;

    BaseClass base;
    void* base_ctx = &base;
    fidl_txn_t dummy_txn;

    fidl_test_fidlutils_BindingTestsProtocol_ops_t ops = {
        .FunctionWithReply = BaseBinder::BindMember<&BaseClass::ConstFnWithReply>,
        .FunctionWithoutReply = BaseBinder::BindMember<&BaseClass::ConstFnWithoutReply>,
    };

    EXPECT_EQ(ops.FunctionWithReply(base_ctx, BaseClass::MAGIC_NUMBER, &dummy_txn), ZX_OK);
    EXPECT_EQ(ops.FunctionWithoutReply(base_ctx, BaseClass::MAGIC_NUMBER), ZX_OK);

    END_TEST;
}

bool VolatileBindMemberTest() {
    BEGIN_TEST;

    BaseClass base;
    void* base_ctx = &base;
    fidl_txn_t dummy_txn;

    fidl_test_fidlutils_BindingTestsProtocol_ops_t ops = {
        .FunctionWithReply = BaseBinder::BindMember<&BaseClass::VolatileFnWithReply>,
        .FunctionWithoutReply = BaseBinder::BindMember<&BaseClass::VolatileFnWithoutReply>,
    };

    EXPECT_EQ(ops.FunctionWithReply(base_ctx, BaseClass::MAGIC_NUMBER, &dummy_txn), ZX_OK);
    EXPECT_EQ(ops.FunctionWithoutReply(base_ctx, BaseClass::MAGIC_NUMBER), ZX_OK);

    END_TEST;
}

bool OverloadedBindMemberTest() {
    BEGIN_TEST;

    BaseClass base;
    void* base_ctx = &base;
    fidl_txn_t dummy_txn;

    fidl_test_fidlutils_BindingTestsProtocol_ops_t ops = {
        .FunctionWithReply = BaseBinder::BindMember<zx_status_t(uint64_t, fidl_txn_t*),
                                                    &BaseClass::OverloadedFnWithReply>,
        .FunctionWithoutReply = BaseBinder::BindMember<zx_status_t(uint64_t),
                                                       &BaseClass::OverloadedFnWithoutReply>,
    };

    EXPECT_EQ(ops.FunctionWithReply(base_ctx, BaseClass::MAGIC_NUMBER, &dummy_txn), ZX_OK);
    EXPECT_EQ(ops.FunctionWithoutReply(base_ctx, BaseClass::MAGIC_NUMBER), ZX_OK);

    END_TEST;
}

bool VirtualBindMemberTest() {
    BEGIN_TEST;

    BaseClass base;
    DerivedClass derived;
    void* base_ctx = &base;
    void* derived_ctx = &derived;
    fidl_txn_t dummy_txn;

    fidl_test_fidlutils_BindingTestsProtocol_ops_t base_ops = {
        .FunctionWithReply = BaseBinder::BindMember<&BaseClass::VirtualFnWithReply>,
        .FunctionWithoutReply = BaseBinder::BindMember<&BaseClass::VirtualFnWithoutReply>,
    };
    fidl_test_fidlutils_BindingTestsProtocol_ops_t derived_ops = {
        .FunctionWithReply = DerivedBinder::BindMember<&DerivedClass::VirtualFnWithReply>,
        .FunctionWithoutReply = DerivedBinder::BindMember<&DerivedClass::VirtualFnWithoutReply>,
    };

    EXPECT_EQ(base_ops.FunctionWithReply(base_ctx, BaseClass::MAGIC_NUMBER, &dummy_txn), ZX_OK);
    EXPECT_EQ(base_ops.FunctionWithoutReply(base_ctx, BaseClass::MAGIC_NUMBER), ZX_OK);

    EXPECT_EQ(derived_ops.FunctionWithReply(derived_ctx, DerivedClass::MAGIC_NUMBER, &dummy_txn),
              ZX_OK);
    EXPECT_EQ(derived_ops.FunctionWithoutReply(derived_ctx, DerivedClass::MAGIC_NUMBER), ZX_OK);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(binding_tests)
RUN_TEST(SimpleBindMemberTest)
RUN_TEST(ConstBindMemberTest)
RUN_TEST(VolatileBindMemberTest)
RUN_TEST(OverloadedBindMemberTest)
RUN_TEST(VirtualBindMemberTest)
END_TEST_CASE(binding_tests)
