// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper.h"

#include <zircon/assert.h>
#include <zircon/types.h>
#include <zxtest/zxtest.h>

// Sanity check that looks for bugs in C macro implementation of ASSERT_*/EXPECT_*. This forces
// the text replacement and allows the compiler to find errors. Otherwise is left to the user
// to find errors once the macro is first used. Also we validate the the assertions return
// and expects dont.
// Tests will fail because we are verifying they actually work as intended, though the
// pass/fail behavior is decided based on Verify functions.

TEST(ZxTestAssertionsTest, Fail) {
    FAIL("Something bad happened.");
    ZX_ASSERT_MSG(_ZXTEST_ABORT_IF_ERROR, "FAIL did not abort test execution");
    ZX_ASSERT_MSG(false, "_ZXTEST_ABORT_IF_ERROR not set on failure.");
}

TEST(ZxTestAssertionsTest, AssertTrueAndFalse) {
    EXPECT_TRUE(true, "EXPECT_TRUE failed.");
    EXPECT_FALSE(false, "EXPECT_FALSE failed.");
    ASSERT_TRUE(true, "ASSERT_TRUE failed.");
    ASSERT_FALSE(false, "ASSERT_FALSE failed.");
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "FAIL did not abort test execution");
}

TEST(ZxTestAssertionsTest, AssertTrueAndFalseFailure) {
    EXPECT_TRUE(false, "EXPECT_TRUE suceed");
    EXPECT_FALSE(true, "EXPECT_FALSE succeed.");
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Assert did not abort test execution");
}

TEST(ZxTestAssertionsTest, AssertFalseFailureFatal) {
    ASSERT_FALSE(true, "ASSERT_FALSE succeed.");
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Assert did not abort test execution");
}

TEST(ZxTestAssertionsTest, AssertTrueFailureFatal) {
    ASSERT_TRUE(false, "ASSERT_TRUE succeed.");
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Assert did not abort test execution");
}

TEST(ZxTestAssertionsTest, AssertEQSuccess) {
    int a = 1;
    int b = 2;

    // Happy cases.
    EXPECT_EQ(1, 1, "EXPECT_EQ identity failed.");
    ASSERT_EQ(1, 1, "ASSERT_EQ identity failed.");
    EXPECT_EQ(a, a, "EXPECT_EQ identity failed.");
    ASSERT_EQ(b, b, "ASSERT_EQ identity failed.");
    // No failures
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
}

TEST(ZxTestAssertionsTest, AssertEQFailure) {
    int a = 1;
    int b = 2;

    EXPECT_EQ(1, 2, "EXPECT_EQ inequality detection succeeded.");
    EXPECT_EQ(a, b, "EXPECT_EQ inequality detection succeeded.");
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Expect treated as fatal error.");
}

TEST(ZxTestAssertionsTest, AssertEQFailureFatal) {
    ASSERT_EQ(1, 2, "ASSERT_EQ inequality detection succeeded.");
    ZX_ASSERT_MSG(false, "Fatal assertion failed to return.\n");
}

TEST(ZxTestAssertionsTest, AssertNESuccess) {
    int a = 1;
    int b = 2;

    // Happy cases.
    EXPECT_NE(1, 2, "EXPECT_NE inequality detection succeeded.");
    EXPECT_NE(a, b, "EXPECT_NE identity detection succeeded.");
    // No failures
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
}

TEST(ZxTestAssertionsTest, AssertNEFailure) {
    int a = 1;

    EXPECT_NE(1, 1, "EXPECT_NE identity failed.");
    EXPECT_NE(a, a, "EXPECT_NE identity failed.");
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Expect treated as fatal error.");
}

TEST(ZxTestAssertionsTest, AssertNEFailureFatal) {
    int a = 1;
    int b = 1;
    ASSERT_NE(a, b, "ASSERT_NE identity detection succeeded.");
    ZX_ASSERT_MSG(false, "Fatal assertion failed to return.\n");
}

TEST(ZxTestAssertionsTest, AssertLT) {
    int a = 1;
    int b = 2;

    // Happy cases.
    ASSERT_LT(1, 2, "ASSERT_LT failed.");
    EXPECT_LT(a, b, "EXPECT_LT failed.");
    // No failures
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
}

TEST(ZxTestAssertionsTest, AssertLTFailure) {
    int a = 1;
    int b = 2;

    EXPECT_LT(2, 1, "EXPECT_LT failed.");
    EXPECT_LT(b, a, "EXPECT_LT failed.");
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Expect treated as fatal error.");
}

TEST(ZxTestAssertionsTest, AssertLTFailureFatal) {
    int a = 1;
    int b = 2;

    ASSERT_LT(b, a, "EXPECT_LT failed.");
    ZX_ASSERT_MSG(_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
}

TEST(ZxTestAssertionsTest, AssertLE) {
    int a = 1;
    int b = 2;

    // Happy cases.
    ASSERT_LE(1, 2, "ASSERT_LE failed.");
    ASSERT_LE(1, 1, "ASSERT_LE failed.");
    EXPECT_LE(a, b, "EXPECT_LE failed.");
    EXPECT_LE(a, a, "EXPECT_LE failed.");
    // No failures
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
}

TEST(ZxTestAssertionsTest, AssertLEFailure) {
    int a = 1;
    int b = 2;

    EXPECT_LE(2, 1, "EXPECT_LE failed.");
    EXPECT_LE(b, a, "EXPECT_LE failed.");
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Expect treated as fatal error.");
}

TEST(ZxTestAssertionsTest, AssertLEFailureFatal) {
    int a = 1;
    int b = 2;

    ASSERT_LE(b, a, "EXPECT_LE failed.");
    ZX_ASSERT_MSG(_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
}

TEST(ZxTestAssertionsTest, AssertGT) {
    int a = 1;
    int b = 2;

    // Happy cases.
    EXPECT_GT(2, 1, "EXPECT_GT failed.");
    EXPECT_GT(b, a, "EXPECT_GT failed.");
    // No failures
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
}

TEST(ZxTestAssertionsTest, AssertGTFailure) {
    int a = 1;
    int b = 2;

    EXPECT_GT(a, b, "EXPECT_GT succeeded.");
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Expect treated as fatal error.");
    ASSERT_GT(1, 2, "ASSERT_GT succeeded.");
    ZX_ASSERT_MSG(_ZXTEST_ABORT_IF_ERROR, "Assert did not abort the test.");
}

TEST(ZxTestAssertionsTest, AssertGTFailureFatal) {
    int a = 1;
    int b = 2;

    ASSERT_GT(a, b, "EXPECT_GT failed.");
    ZX_ASSERT_MSG(_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
}

TEST(ZxTestAssertionsTest, AssertGE) {
    int a = 1;
    int b = 2;

    // Happy cases.
    ASSERT_GE(2, 1, "ASSERT_GE failed.");
    ASSERT_GE(1, 1, "ASSERT_GE failed.");
    EXPECT_GE(b, a, "EXPECT_GE failed.");
    EXPECT_GE(a, a, "EXPECT_GE failed.");
    // No failures
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
}

TEST(ZxTestAssertionsTest, AssertGEFailure) {
    int a = 1;
    int b = 2;

    EXPECT_GE(1, 2, "EXPECT_GE failed.");
    EXPECT_GE(a, b, "EXPECT_GE failed.");
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Expect treated as fatal error.");
}

TEST(ZxTestAssertionsTest, AssertGEFailureFatal) {
    int a = 1;
    int b = 2;

    ASSERT_GE(a, b, "EXPECT_GE failed.");
    ZX_ASSERT_MSG(_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
}

TEST(ZXTestAssertionTest, AssertStrEq) {
    const char* str1 = "a";
    const char* str2 = "a";

    EXPECT_STR_EQ(str1, str2, "ASSERT_STR_EQ failed to identify equal strings.");
    EXPECT_STR_EQ(str1, str1, "ASSERT_STR_EQ failed to identify equal strings.");
    ASSERT_STR_EQ(str1, str2, "ASSERT_STR_EQ failed to identify equal strings.");
    ASSERT_STR_EQ(str1, str1, "ASSERT_STR_EQ failed to identify equal strings.");
    // No failures
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
}

TEST(ZXTestAssertionTest, AssertStrEqFailures) {
    const char* str1 = "a";
    const char* str2 = "b";

    EXPECT_STR_EQ(str1, str2, "ASSERT_STR_EQ failed to identify equal strings.");
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
    ASSERT_STR_EQ(str1, str2, "ASSERT_STR_EQ failed to identify equal strings.");
    ZX_ASSERT_MSG(false, "Assert was did not abort test.");
}

TEST(ZXTestAssertionTest, AssertNotNull) {
    void* a = reinterpret_cast<void*>(0x01);

    ASSERT_NOT_NULL(a, "ASSERT_NOT_NULL failed to identify nullptr.");
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
}

TEST(ZXTestAssertionTest, AssertNotNullFailures) {
    char* a = nullptr;

    EXPECT_NOT_NULL(a, "EXPECT_NOT_NULL identified nullptr.");
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
    ASSERT_NOT_NULL(a, "ASSERT_NOT_NULL identified nullptr.");
    ZX_ASSERT_MSG(_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
}

TEST(ZXTestAssertionTest, AssertNull) {
    char* a = nullptr;

    ASSERT_NULL(a, "ASSERT_NOT_NULL identified nullptr.");
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
}

TEST(ZXTestAssertionTest, AssertNullFailures) {
    char b;
    char* a = &b;

    EXPECT_NULL(a, "EXPECT_NOT_NULL identified nullptr.");
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "EXPECT marked the the test as error.");
    ASSERT_NULL(a, "ASSERT_NOT_NULL identified nullptr.");
    ZX_ASSERT_MSG(_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
}

TEST(ZXTestAssertionTest, AssertOk) {
    zx_status_t status = ZX_OK;

    ASSERT_OK(status, "ASSERT_OK failed to identify ZX_OK.");
    // Lot of time there are overloaded return types, and we consider only negative numbers
    // as errors.
    ASSERT_OK(4, "ASSERT_OK failed to identify ZX_OK.");
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
}

TEST(ZXTestAssertionTest, AssertOkFailures) {
    zx_status_t status = ZX_ERR_BAD_STATE;

    EXPECT_OK(status, "ASSERT_OK failed to identify ZX_OK.");
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Expect marked as fatal error.");
    ASSERT_OK(status, "ASSERT_OK failed to identify ZX_OK.");
    ZX_ASSERT_MSG(_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
}

TEST(ZXTestAssertionTest, AssertBytesEq) {
    struct mytype {
        int a;
        int b;
    };
    struct mytype a, b;
    a.a = 0;
    a.b = 1;
    b.a = 0;
    b.b = 1;

    ASSERT_BYTES_EQ(&a, &a, sizeof(mytype), "ASSERT_BYTES_EQ identity failed.");
    ASSERT_BYTES_EQ(&a, &b, sizeof(mytype), "ASSERT_BYTES_EQ identity failed.");
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Succesful assert marked as fatal error.");
    b.b = 2;
    ASSERT_BYTES_NE(&a, &b, sizeof(struct mytype), "ASSERT_BYTES_EQ identity failed.");
    ASSERT_BYTES_EQ(&a, &b, sizeof(mytype), "ASSERT_BYTES_EQ identity failed.");
    ZX_ASSERT_MSG(_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
}

TEST(ZXTestAssertionTest, AssertBytesEqArray) {
    int a[] = {1, 2, 3, 4, 5};
    int b[] = {1, 2, 3, 4, 5};
    int c[] = {1, 2, 3, 4, 6};

    ASSERT_BYTES_EQ(a, a, sizeof(int) * 5, "ASSERT_BYTES_EQ identity failed.");
    ASSERT_BYTES_EQ(a, b, sizeof(int) * 5, "ASSERT_BYTES_EQ identity failed.");
    ZX_ASSERT_MSG(!_ZXTEST_ABORT_IF_ERROR, "Succesful assert marked as fatal error.");
    ASSERT_BYTES_EQ(a, c, sizeof(int) * 5, "ASSERT_BYTES_EQ identity failed.");
    ZX_ASSERT_MSG(false, "Failed to detect inequality.");
}

TEST(ZXTestAssertionTest, AssertSingleCall) {
    int called = 0;
    int getter_called = 0;
    auto increase = [&called]() { return ++called; };
    auto getter = [&getter_called, &called]() {
        getter_called++;
        return called;
    };

    EXPECT_EQ(getter(), increase());
    ZX_ASSERT_MSG(called == 1, "Assertion evaluating multiple times.");
    ZX_ASSERT_MSG(getter_called == 1, "Assertion evaluating multiple times.");
}

TEST(ZXTestAssertionTest, AssertBytesSingleCall) {
    int called = 0;
    int getter_called = 0;
    auto increase = [&called]() {
        ++called;
        return &called;
    };
    auto getter = [&getter_called, &called]() {
        getter_called++;
        return &called;
    };

    EXPECT_BYTES_EQ(getter(), increase(), sizeof(int));
    ZX_ASSERT_MSG(called == 1, "Assertion evaluating multiple times.");
    ZX_ASSERT_MSG(getter_called == 1, "Assertion evaluating multiple times.");
}
