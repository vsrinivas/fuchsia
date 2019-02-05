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
    TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                     "FAIL(...) macro did not abort test execution.");
    FAIL("Something bad happened.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertTrueAndFalse) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS,
                     "EXPECT/ASSERT_TRUE/FALSE returned on success.");
    EXPECT_TRUE(true, "EXPECT_TRUE failed.");
    EXPECT_FALSE(false, "EXPECT_FALSE failed.");
    ASSERT_TRUE(true, "ASSERT_TRUE failed.");
    ASSERT_FALSE(false, "ASSERT_FALSE failed.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertTrueAndFalseFailure) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS,
                     "EXPECT/ASSERT_TRUE/FALSE returned on success.");
    EXPECT_TRUE(false, "EXPECT_TRUE suceed");
    EXPECT_FALSE(true, "EXPECT_FALSE succeed.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertFalseFailureFatal) {
    TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                     "ASSERT_FALSE failed to abort test execution.");
    ASSERT_FALSE(true, "ASSERT_FALSE success.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertTrueFailureFatal) {
    TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                     "ASSERT_TRUE failed to abort test execution.");
    ASSERT_TRUE(false, "ASSERT_TRUE succeed.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertEQSuccess) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "ASSERT/EXPECT_EQ aborted test on success.");
    int a = 1;
    int b = 2;

    // Happy cases.
    EXPECT_EQ(1, 1, "EXPECT_EQ identity failed.");
    ASSERT_EQ(1, 1, "ASSERT_EQ identity failed.");
    EXPECT_EQ(a, a, "EXPECT_EQ identity failed.");
    ASSERT_EQ(b, b, "ASSERT_EQ identity failed.");
    // No failures
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertEQFailure) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_EQ aborted execution.");
    int a = 1;
    int b = 2;

    EXPECT_EQ(1, 2, "EXPECT_EQ inequality detection succeeded.");
    EXPECT_EQ(a, b, "EXPECT_EQ inequality detection succeeded.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertEQFailureFatal) {
    TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "ASSERT_EQ did not abort test execution.");
    ASSERT_EQ(1, 2, "ASSERT_EQ inequality detection succeeded.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertNESuccess) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "EXPECT_NE aborted test execution.");
    int a = 1;
    int b = 2;

    // Happy cases.
    EXPECT_NE(1, 2, "EXPECT_NE inequality detection succeeded.");
    EXPECT_NE(a, b, "EXPECT_NE inequality detection succeeded.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertNEFailure) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_NE aborted test execution.");
    int a = 1;

    EXPECT_NE(1, 1, "EXPECT_NE equality detection suceeded.");
    EXPECT_NE(a, a, "EXPECT_NE equality detection suceeded.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertNEFailureFatal) {
    TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                     "ASSERT_NE  did not abort test execution.");
    int a = 1;
    int b = 1;
    ASSERT_NE(a, b, "ASSERT_NE equality detection succeeded.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertLT) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "ASSERT_LT did not abort test execution.");
    int a = 1;
    int b = 2;

    // Happy cases.
    ASSERT_LT(1, 2, "ASSERT_LT failed.");
    EXPECT_LT(a, b, "EXPECT_LT failed.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertLTFailure) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "ASSERT_LT  did not abort test execution.");
    int a = 1;
    int b = 2;

    EXPECT_LT(2, 1, "EXPECT_LT failed.");
    EXPECT_LT(b, a, "EXPECT_LT failed.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertLTFailureFatal) {
    TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "ASSERT_LT did not abort test execution.");
    int a = 1;
    int b = 2;

    ASSERT_LT(b, a, "EXPECT_LT failed.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertLE) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS,
                     "ASSERT/EXPECT_LE aborted test execution on success.");
    int a = 1;
    int b = 2;

    // Happy cases.
    ASSERT_LE(1, 2, "ASSERT_LE failed.");
    ASSERT_LE(1, 1, "ASSERT_LE failed.");
    EXPECT_LE(a, b, "EXPECT_LE failed.");
    EXPECT_LE(a, a, "EXPECT_LE failed.");
    // No failures
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertLEFailure) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_LE aborted test execution.");
    int a = 1;
    int b = 2;

    EXPECT_LE(2, 1, "EXPECT_LE failed.");
    EXPECT_LE(b, a, "EXPECT_LE failed.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertLEFailureFatal) {
    TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "ASSERT_LE did not abort test execution.");
    int a = 1;
    int b = 2;

    ASSERT_LE(b, a, "EXPECT_LE failed.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertGT) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "EXPECT_GT aborted test execution on success.");
    int a = 1;
    int b = 2;

    EXPECT_GT(2, 1, "EXPECT_GT failed.");
    EXPECT_GT(b, a, "EXPECT_GT failed.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertGTFailure) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_GT aborted test execution.");
    int a = 1;
    int b = 2;

    EXPECT_GT(a, b, "EXPECT_GT succeeded.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertGTFatalFailure) {
    TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "EXPECT_GT did aborted test execution.");
    int a = 1;
    int b = 2;

    ASSERT_GT(a, b, "ASSERT_GT succeeded.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertGE) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS,
                     "ASSERT/EXPECT_GE aborted test execution on success.");
    int a = 1;
    int b = 2;

    ASSERT_GE(2, 1, "ASSERT_GE failed.");
    ASSERT_GE(1, 1, "ASSERT_GE failed.");
    EXPECT_GE(b, a, "EXPECT_GE failed.");
    EXPECT_GE(a, a, "EXPECT_GE failed.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertGEFailure) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS,
                     "ASSERT/EXPECT_GE aborted test execution on success.");
    int a = 1;
    int b = 2;

    EXPECT_GE(1, 2, "EXPECT_GE failed.");
    EXPECT_GE(a, b, "EXPECT_GE failed.");
    TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertGEFailureFatal) {
    TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                     "ASSERT/EXPECT_GE aborted test execution on success.");
    int a = 1;
    int b = 2;

    ASSERT_GE(a, b, "EXPECT_GE failed.");
    ZX_ASSERT_MSG(_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
    TEST_CHECKPOINT();
}

TEST(ZXTestAssertionTest, AssertStrEq) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS,
                     "ASSERT/EXPECT_STR_EQ aborted test execution on success.");
    const char* str1 = "a";
    const char* str2 = "a";

    EXPECT_STR_EQ(str1, str2, "ASSERT_STR_EQ failed to identify equal strings.");
    EXPECT_STR_EQ(str1, str1, "ASSERT_STR_EQ failed to identify equal strings.");
    ASSERT_STR_EQ(str1, str2, "ASSERT_STR_EQ failed to identify equal strings.");
    ASSERT_STR_EQ(str1, str1, "ASSERT_STR_EQ failed to identify equal strings.");
    TEST_CHECKPOINT();
}

TEST(ZXTestAssertionTest, AssertStrEqFailure) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_STR_EQ aborted test execution.");
    const char* str1 = "a";
    const char* str2 = "b";

    EXPECT_STR_EQ(str1, str2, "ASSERT_STR_EQ failed to identify equal strings.");
    TEST_CHECKPOINT();
}

TEST(ZXTestAssertionTest, AssertStrEqFatalFailure) {
    TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                     "ASSERT/EXPECT_STR_EQ aborted test execution on success.");
    const char* str1 = "a";
    const char* str2 = "b";

    ASSERT_STR_EQ(str1, str2, "ASSERT_STR_EQ failed to identify equal strings.");
    TEST_CHECKPOINT();
}

TEST(ZXTestAssertionTest, AssertNotNull) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS,
                     "ASSERT/EXPECT_NOT_NULL aborted test execution on success.");
    char a;

    EXPECT_NOT_NULL(&a, "ASSERT_NOT_NULL failed to identify NULL.");
    ASSERT_NOT_NULL(&a, "ASSERT_NOT_NULL failed to identify NULL.");
    TEST_CHECKPOINT();
}

TEST(ZXTestAssertionTest, AssertNotNullFailure) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_NOT_NULL aborted test execution.");
    char* a = nullptr;

    EXPECT_NOT_NULL(a, "EXPECT_NOT_NULL identified NULL.");
    TEST_CHECKPOINT();
}

TEST(ZXTestAssertionTest, AssertNotNullFatalFailure) {
    TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                     "ASSERT_NOT_NULL did not abort test execution.");
    char* a = nullptr;

    ASSERT_NOT_NULL(a, "ASSERT_NOT_NULL identified NULL.");
    TEST_CHECKPOINT();
}

TEST(ZXTestAssertionTest, AssertNull) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS,
                     "ASSERT/EXPECT_NULL aborted test execution on success.");
    char* a = nullptr;

    ASSERT_NULL(a, "ASSERT_NULL did not identify NULL.");
    TEST_CHECKPOINT();
}

TEST(ZXTestAssertionTest, AssertNullFailure) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_NULL aborted test execution.");
    char b;
    char* a = &b;

    EXPECT_NULL(a, "EXPECT_NOT_NULL identified NULL.");
    TEST_CHECKPOINT();
}

TEST(ZXTestAssertionTest, AssertNullFatalFailure) {
    TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                     "ASSERT_NULL did not abort test execution.");
    char b;
    char* a = &b;

    ASSERT_NULL(a, "ASSERT_NOT_NULL identified NULL.");
    TEST_CHECKPOINT();
}

TEST(ZXTestAssertionTest, AssertOk) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS,
                     "ASSERT/EXPECT_OK aborted test execution on success.");
    zx_status_t status = ZX_OK;

    EXPECT_OK(status, "EXPECT_OK failed to identify ZX_OK.");
    ASSERT_OK(status, "ASSERT_OK failed to identify ZX_OK.");
    // Lot of time there are overloaded return types, and we consider only negative numbers
    // as errors.
    EXPECT_OK(4, "EXPECT_OK failed to identify ZX_OK.");
    ASSERT_OK(4, "ASSERT_OK failed to identify ZX_OK.");

    TEST_CHECKPOINT();
}

TEST(ZXTestAssertionTest, AssertOkFailure) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_OK aborted test execution.");
    zx_status_t status = ZX_ERR_BAD_STATE;

    EXPECT_OK(status, "EXPECT_OK failed to identify error.");
    TEST_CHECKPOINT();
}

TEST(ZXTestAssertionTest, AssertOkFatalFailure) {
    TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "EXPECT_OK aborted test execution.");
    zx_status_t status = ZX_ERR_BAD_STATE;

    ASSERT_OK(status, "ASSERT_OK failed to identify error.");
    TEST_CHECKPOINT();
}

struct mytype {
    int a;
    int b;
};

TEST(ZXTestAssertionTest, AssertBytesEq) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS,
                     "ASSERT/EXPECT_BYTES_EQ aborted test execution on success.");
    mytype a, b;
    a.a = 0;
    a.b = 1;
    b.a = 0;
    b.b = 1;

    ASSERT_BYTES_EQ(&a, &a, sizeof(mytype), "ASSERT_BYTES_EQ identity failed.");
    EXPECT_BYTES_EQ(&a, &a, sizeof(mytype), "EXPECT_BYTES_EQ identity failed.");
    ASSERT_BYTES_EQ(&a, &b, sizeof(mytype), "ASSERT_BYTES_EQ identity failed.");
    EXPECT_BYTES_EQ(&a, &b, sizeof(mytype), "EXPECT_BYTES_EQ identity failed.");
    TEST_CHECKPOINT();
}

TEST(ZXTestAssertionTest, AssertBytesEqFailure) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_OK aborted test execution.");
    mytype a, b;
    a.a = 0;
    a.b = 1;
    b.a = 0;
    b.b = 2;

    EXPECT_BYTES_EQ(&a, &b, sizeof(mytype), "ASSERT_BYTES_EQ identity failed.");
    TEST_CHECKPOINT();
}

TEST(ZXTestAssertionTest, AssertBytesEqFatalFailure) {
    TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "EXPECT_OK aborted test execution.");
    mytype a, b;
    a.a = 0;
    a.b = 1;
    b.a = 0;
    b.b = 2;

    ASSERT_BYTES_EQ(&a, &b, sizeof(mytype), "ASSERT_BYTES_EQ identity failed.");
    TEST_CHECKPOINT();
}

TEST(ZXTestAssertionTest, AssertBytesNe) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS,
                     "ASSERT/EXPECT_BYTES_NE aborted test execution on success.");
    mytype a, b;
    a.a = 0;
    a.b = 1;
    b.a = 0;
    b.b = 2;

    ASSERT_BYTES_NE(&a, &b, sizeof(mytype), "ASSERT_BYTES_NE identity failed.");
    EXPECT_BYTES_NE(&a, &b, sizeof(mytype), "EXPECT_BYTES_NE identity failed.");
    TEST_CHECKPOINT();
}

TEST(ZXTestAssertionTest, AssertBytesNeFailure) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_OK aborted test execution.");
    mytype a, b;
    a.a = 0;
    a.b = 1;
    b.a = 0;
    b.b = 1;

    EXPECT_BYTES_NE(&a, &b, sizeof(mytype), "ASSERT_BYTES_NE identity failed.");
    TEST_CHECKPOINT();
}

TEST(ZXTestAssertionTest, AssertBytesNeFatalFailure) {
    TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "EXPECT_OK aborted test execution.");
    mytype a, b;
    a.a = 0;
    a.b = 1;
    b.a = 0;
    b.b = 1;

    ASSERT_BYTES_NE(&a, &b, sizeof(mytype), "ASSERT_BYTES_NE identity failed.");
    TEST_CHECKPOINT();
}

TEST(ZXTestAssertionTest, AssertBytesEqArray) {
    TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS,
                     "ASSERT_BYTES_EQ failed to compare array contents.");
    int a[] = {1, 2, 3, 4, 5};
    int b[] = {1, 2, 3, 4, 5};

    ASSERT_BYTES_EQ(a, a, sizeof(int) * 5, "ASSERT_BYTES_EQ identity failed.");
    ASSERT_BYTES_EQ(a, b, sizeof(int) * 5, "ASSERT_BYTES_EQ identity failed.");
    TEST_CHECKPOINT();
}

TEST(ZXTestAssertionTest, AssertBytesEqArrayFailure) {
    TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                     "ASSERT_BYTES_EQ did not abort test execution.");
    int a[] = {1, 2, 3, 4, 5};
    int b[] = {1, 2, 3, 4, 6};

    ASSERT_BYTES_EQ(a, b, sizeof(int) * 5, "ASSERT_BYTES_EQ identified different arrays.");
    TEST_CHECKPOINT();
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
