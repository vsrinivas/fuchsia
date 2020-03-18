// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>
#include <zircon/types.h>

#include <zxtest/cpp/internal.h>
#include <zxtest/zxtest.h>

#include "helper.h"

// Sanity check that looks for bugs in C macro implementation of ASSERT_*/EXPECT_*. This forces
// the text replacement and allows the compiler to find errors. Otherwise is left to the user
// to find errors once the macro is first used. Also we validate that the assertions return
// and expects don't.
// Tests will fail because we are verifying they actually work as intended, though the
// pass/fail behavior is decided based on Verify functions.
namespace {
TEST(ZxTestAssertionTest, Fail) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                   "FAIL(...) macro did not abort test execution.");
  FAIL("Something bad happened.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertTrueAndFalse) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "EXPECT/ASSERT_TRUE/FALSE returned on success.");
  EXPECT_TRUE(true, "EXPECT_TRUE failed.");
  EXPECT_FALSE(false, "EXPECT_FALSE failed.");
  ASSERT_TRUE(true, "ASSERT_TRUE failed.");
  ASSERT_FALSE(false, "ASSERT_FALSE failed.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertTrueAndFalseFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT/ASSERT_TRUE/FALSE returned on success.");
  EXPECT_TRUE(false, "EXPECT_TRUE succeed");
  EXPECT_FALSE(true, "EXPECT_FALSE succeed.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertFalseFailureFatal) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                   "ASSERT_FALSE failed to abort test execution.");
  ASSERT_FALSE(true, "ASSERT_FALSE success.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertTrueFailureFatal) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                   "ASSERT_TRUE failed to abort test execution.");
  ASSERT_TRUE(false, "ASSERT_TRUE succeed.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertEQSuccess) {
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

TEST(ZxTestAssertionTest, AssertEQFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_EQ aborted execution.");
  int a = 1;
  int b = 2;

  EXPECT_EQ(1, 2, "EXPECT_EQ inequality detection succeeded.");
  EXPECT_EQ(a, b, "EXPECT_EQ inequality detection succeeded.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertEQFailureFatal) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "ASSERT_EQ did not abort test execution.");
  ASSERT_EQ(1, 2, "ASSERT_EQ inequality detection succeeded.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertNESuccess) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "EXPECT_NE aborted test execution.");
  int a = 1;
  int b = 2;

  // Happy cases.
  EXPECT_NE(1, 2, "EXPECT_NE inequality detection succeeded.");
  EXPECT_NE(a, b, "EXPECT_NE inequality detection succeeded.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertNEFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_NE aborted test execution.");
  int a = 1;

  EXPECT_NE(1, 1, "EXPECT_NE equality detection succeeded.");
  EXPECT_NE(a, a, "EXPECT_NE equality detection succeeded.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertNEFailureFatal) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "ASSERT_NE  did not abort test execution.");
  int a = 1;
  int b = 1;
  ASSERT_NE(a, b, "ASSERT_NE equality detection succeeded.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertLT) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "ASSERT_LT did not abort test execution.");
  int a = 1;
  int b = 2;

  // Happy cases.
  ASSERT_LT(1, 2, "ASSERT_LT failed.");
  EXPECT_LT(a, b, "EXPECT_LT failed.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertLTFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "ASSERT_LT  did not abort test execution.");
  int a = 1;
  int b = 2;

  EXPECT_LT(2, 1, "EXPECT_LT failed.");
  EXPECT_LT(b, a, "EXPECT_LT failed.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertLTFailureFatal) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "ASSERT_LT did not abort test execution.");
  int a = 1;
  int b = 2;

  ASSERT_LT(b, a, "EXPECT_LT failed.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertLE) {
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

TEST(ZxTestAssertionTest, AssertLEFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_LE aborted test execution.");
  int a = 1;
  int b = 2;

  EXPECT_LE(2, 1, "EXPECT_LE failed.");
  EXPECT_LE(b, a, "EXPECT_LE failed.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertLEFailureFatal) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "ASSERT_LE did not abort test execution.");
  int a = 1;
  int b = 2;

  ASSERT_LE(b, a, "EXPECT_LE failed.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertGT) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "EXPECT_GT aborted test execution on success.");
  int a = 1;
  int b = 2;

  EXPECT_GT(2, 1, "EXPECT_GT failed.");
  EXPECT_GT(b, a, "EXPECT_GT failed.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertGTFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_GT aborted test execution.");
  int a = 1;
  int b = 2;

  EXPECT_GT(a, b, "EXPECT_GT succeeded.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertGTFatalFailure) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "EXPECT_GT did aborted test execution.");
  int a = 1;
  int b = 2;

  ASSERT_GT(a, b, "ASSERT_GT succeeded.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertGE) {
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

TEST(ZxTestAssertionTest, AssertGEFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS,
                   "ASSERT/EXPECT_GE aborted test execution on success.");
  int a = 1;
  int b = 2;

  EXPECT_GE(1, 2, "EXPECT_GE failed.");
  EXPECT_GE(a, b, "EXPECT_GE failed.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertGEFailureFatal) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                   "ASSERT/EXPECT_GE aborted test execution on success.");
  int a = 1;
  int b = 2;

  ASSERT_GE(a, b, "EXPECT_GE failed.");
  ZX_ASSERT_MSG(_ZXTEST_ABORT_IF_ERROR, "Assert was did not abort test.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertStrEq) {
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

TEST(ZxTestAssertionTest, AssertStrNe) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS,
                   "ASSERT/EXPECT_STR_EQ aborted test execution on success.");
  const char* str1 = "a";
  const char* str2 = "b";

  EXPECT_STR_NE(str1, str2, "EXPECT_STR_NE failed to identify different strings.");
  ASSERT_STR_NE(str1, str2, "ASSERT_STR_NE failed to identify different strings.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertStrEqFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_STR_EQ aborted test execution.");
  const char* str1 = "a";
  const char* str2 = "b";

  EXPECT_STR_EQ(str1, str2, "ASSERT_STR_EQ failed to identify equal strings.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertStrEqFatalFailure) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                   "ASSERT/EXPECT_STR_EQ aborted test execution on success.");
  const char* str1 = "a";
  const char* str2 = "b";

  ASSERT_STR_EQ(str1, str2, "ASSERT_STR_EQ failed to identify equal strings.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertExpectSubStr) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS,
                   "ASSERT/EXPECT_SUBSTR aborted test execution on success.");
  const char* str = "abc";
  const char* target = "bc";

  EXPECT_SUBSTR(str, target, "EXPECT_SUBSTR failed to find substring.");
  ASSERT_SUBSTR(str, target, "ASSERT_SUBSTR failed to find substring.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, ExpectSubStrFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_SUBSTR aborted test execution.");
  const char* str = "abc";
  const char* target = "bcd";

  EXPECT_SUBSTR(str, target, "EXPECT_SUBSTR unexpectedly found substring.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertSubStrFatalFailure) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                   "ASSERT_SUBSTR aborted test execution on success.");
  const char* str = "abc";
  const char* target = "bcd";

  ASSERT_SUBSTR(str, target, "ASSERT_SUBSTR unexpectedly found substring.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertExpectNotSubStr) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS,
                   "ASSERT/EXPECT_SUBSTR aborted test execution on success.");
  const char* str = "abc";
  const char* target = "bcd";

  EXPECT_NOT_SUBSTR(str, target, "EXPECT_SUBSTR failed to find substring.");
  ASSERT_NOT_SUBSTR(str, target, "ASSERT_SUBSTR failed to find substring.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, ExpectNotSubStrFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_SUBSTR aborted test execution.");
  const char* str = "abc";
  const char* target = "bc";

  EXPECT_NOT_SUBSTR(str, target, "EXPECT_SUBSTR unexpectedly found substring.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertNotSubStrFatalFailure) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                   "ASSERT_SUBSTR aborted test execution on success.");
  const char* str = "abc";
  const char* target = "bc";

  ASSERT_NOT_SUBSTR(str, target, "ASSERT_SUBSTR unexpectedly found substring.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertNotNull) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS,
                   "ASSERT/EXPECT_NOT_NULL aborted test execution on success.");
  char a;

  EXPECT_NOT_NULL(&a, "ASSERT_NOT_NULL failed to identify NULL.");
  ASSERT_NOT_NULL(&a, "ASSERT_NOT_NULL failed to identify NULL.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertNotNullFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_NOT_NULL aborted test execution.");
  char* a = nullptr;

  EXPECT_NOT_NULL(a, "EXPECT_NOT_NULL identified NULL.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertNotNullFatalFailure) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                   "ASSERT_NOT_NULL did not abort test execution.");
  char* a = nullptr;

  ASSERT_NOT_NULL(a, "ASSERT_NOT_NULL identified NULL.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertNull) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS,
                   "ASSERT/EXPECT_NULL aborted test execution on success.");
  char* a = nullptr;

  ASSERT_NULL(a, "ASSERT_NULL did not identify NULL.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertNullFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_NULL aborted test execution.");
  char b;
  char* a = &b;

  EXPECT_NULL(a, "EXPECT_NOT_NULL identified NULL.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertNullFatalFailure) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "ASSERT_NULL did not abort test execution.");
  char b;
  char* a = &b;

  ASSERT_NULL(a, "ASSERT_NOT_NULL identified NULL.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertOk) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS,
                   "ASSERT/EXPECT_OK aborted test execution on success.");
  zx_status_t status = ZX_OK;

  EXPECT_OK(status, "EXPECT_OK failed to identify ZX_OK.");
  ASSERT_OK(status, "ASSERT_OK failed to identify ZX_OK.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertOkFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_OK aborted test execution.");
  zx_status_t status = ZX_ERR_BAD_STATE;

  EXPECT_OK(status, "EXPECT_OK failed to identify error.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertOkFatalFailure) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "EXPECT_OK aborted test execution.");
  zx_status_t status = ZX_ERR_BAD_STATE;

  ASSERT_OK(status, "ASSERT_OK failed to identify error.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertOkWithOverloadedReturnTypeFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_OK aborted test execution.");

  EXPECT_OK(4, "EXPECT_OK failed to identify error.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertOkWithOverloadedReturnTypeFatalFailure) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "ASSERT_OK aborted test execution.");

  ASSERT_OK(4, "ASSERT_OK failed to identify error.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertNotOk) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS,
                   "ASSERT/EXPECT_NOT_OK aborted test execution on success.");
  zx_status_t status = ZX_ERR_BAD_STATE;

  EXPECT_NOT_OK(status, "EXPECT_NOT_OK failed to identify ZX_NOT_OK.");
  ASSERT_NOT_OK(status, "ASSERT_NOT_OK failed to identify ZX_NOT_OK.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertNotOkFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_NOT_OK aborted test execution.");
  zx_status_t status = ZX_OK;

  EXPECT_NOT_OK(status, "EXPECT_NOT_OK failed to identify error.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertNotOkFatalFailure) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "ASSERT_NOT_OK aborted test execution.");
  zx_status_t status = ZX_OK;

  ASSERT_NOT_OK(status, "ASSERT_NOT_OK failed to identify error.");
  TEST_CHECKPOINT();
}

struct mytype {
  int a;
  int b;
};

TEST(ZxTestAssertionTest, AssertBytesEq) {
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

TEST(ZxTestAssertionTest, AssertBytesEqFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_OK aborted test execution.");
  mytype a, b;
  a.a = 0;
  a.b = 1;
  b.a = 0;
  b.b = 2;

  EXPECT_BYTES_EQ(&a, &b, sizeof(mytype), "ASSERT_BYTES_EQ identity failed.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertBytesEqFatalFailure) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "EXPECT_OK aborted test execution.");
  mytype a, b;
  a.a = 0;
  a.b = 1;
  b.a = 0;
  b.b = 2;

  ASSERT_BYTES_EQ(&a, &b, sizeof(mytype), "ASSERT_BYTES_EQ identity failed.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertBytesNe) {
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

TEST(ZxTestAssertionTest, AssertBytesNeFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_OK aborted test execution.");
  mytype a, b;
  a.a = 0;
  a.b = 1;
  b.a = 0;
  b.b = 1;

  EXPECT_BYTES_NE(&a, &b, sizeof(mytype), "ASSERT_BYTES_NE identity failed.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertBytesNeFatalFailure) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "EXPECT_OK aborted test execution.");
  mytype a, b;
  a.a = 0;
  a.b = 1;
  b.a = 0;
  b.b = 1;

  ASSERT_BYTES_NE(&a, &b, sizeof(mytype), "ASSERT_BYTES_NE identity failed.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertBytesEqArray) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS,
                   "ASSERT_BYTES_EQ failed to compare array contents.");
  int a[] = {1, 2, 3, 4, 5};
  int b[] = {1, 2, 3, 4, 5};

  ASSERT_BYTES_EQ(a, a, sizeof(int) * 5, "ASSERT_BYTES_EQ identity failed.");
  ASSERT_BYTES_EQ(a, b, sizeof(int) * 5, "ASSERT_BYTES_EQ identity failed.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertBytesEqArrayFailure) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                   "ASSERT_BYTES_EQ did not abort test execution.");
  int a[] = {1, 2, 3, 4, 5};
  int b[] = {1, 2, 3, 4, 6};

  ASSERT_BYTES_EQ(a, b, sizeof(int) * 5, "ASSERT_BYTES_EQ identified different arrays.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertSingleCall) {
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

TEST(ZxTestAssertionTest, AssertBytesSingleCall) {
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

void HelperFnFatal(bool fail) { ASSERT_FALSE(fail, "Expected to fail."); }

TEST(ZxTestAssertionTest, AssertNoFatalFailureWithFatalFailure) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                   "Failed to abort test execution on helper fatal failure.");
  ASSERT_NO_FATAL_FAILURES(HelperFnFatal(true), "HelperFnFatal had a failure. This is expected.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertNoFatalFailureWithoutFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS,
                   "Aborted test execution on helper with no failures.");
  ASSERT_NO_FATAL_FAILURES(HelperFnFatal(false),
                           "HelperFnFatal had a failure. This is not expected.");
  TEST_CHECKPOINT();
}

void HelperFn(bool fail) { EXPECT_FALSE(fail, "Expected to fail."); }

TEST(ZxTestAssertionTest, AssertNoFatalFailureWithFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "Aborted test execution on helper failure.");
  ASSERT_NO_FATAL_FAILURES(HelperFn(true), "HelperFn had a failure. This is expected.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertTrueCoerceTypeToBoolFailure) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "Failed to identify false.");
  int a = 0;
  ASSERT_TRUE(a, "0 coerced to false.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertTrueCoerceTypeToBool) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to identify true.");
  int a = 1;
  ASSERT_TRUE(a, "1 not coerced to true.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertFalseCoerceTypeToBool) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to identify false.");
  int a = 0;
  ASSERT_FALSE(a, "0 not coerced to false.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertFalseCoerceTypeToBoolFailure) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "Failed to identify true.");
  int a = 1;
  ASSERT_FALSE(a, "1 coerced to true.");
  TEST_CHECKPOINT();
}

// Class to be coerced to bool.
class ConverToBool {
 public:
  ConverToBool(bool value) { value_ = value; }
  virtual ~ConverToBool() {}

  operator bool() const { return value_; }

 private:
  bool value_;
};

class ConverToBoolNotCopyable : public ConverToBool {
 public:
  ConverToBoolNotCopyable(bool value) : ConverToBool(value) {}
  ConverToBoolNotCopyable(const ConverToBoolNotCopyable&) = delete;
};

class ConverToBoolNotMoveable : public ConverToBool {
 public:
  ConverToBoolNotMoveable(bool value) : ConverToBool(value) {}
  ConverToBoolNotMoveable(const ConverToBoolNotMoveable&) = delete;
  ConverToBoolNotMoveable(ConverToBoolNotMoveable&&) = delete;
};

TEST(ZxTestAssertionTest, CoerceNullPtrToBoolBase) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to identify false.");
  void* val = nullptr;
  ASSERT_FALSE(val);
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, CoercePtrToBoolBase) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to identify false.");
  char val;
  ASSERT_TRUE(&val);
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, CoerceTypeToBoolBase) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to identify false.");
  ConverToBool val(true);
  ASSERT_TRUE(val);
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, CoerceTypeToBoolNonCopyable) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to identify false.");
  ConverToBoolNotCopyable val(true);
  ASSERT_TRUE(val);
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, CoerceTypeToBoolNonMoveable) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to identify false.");
  ConverToBoolNotMoveable val(true);
  ASSERT_TRUE(val);
  TEST_CHECKPOINT();
}

int SomeFn() { return 0; }

TEST(ZxTestAssertionTest, FunctionPointerNotNull) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to identify false.");
  int (*some_fn)() = &SomeFn;
  ASSERT_NOT_NULL(some_fn);
  EXPECT_NOT_NULL(some_fn);
  ASSERT_EQ(some_fn, &SomeFn);
  ASSERT_NE(some_fn, nullptr);
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, FunctionPointerNull) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to identify nullptr.");
  int (*some_fn)() = nullptr;
  ASSERT_NULL(some_fn);
  EXPECT_NULL(some_fn);
  ASSERT_NE(some_fn, &SomeFn);
  ASSERT_EQ(some_fn, nullptr);
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, FunctionPointerNotNullFail) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "Failed to identify nullptr.");
  int (*some_fn)() = &SomeFn;
  ASSERT_NULL(some_fn);
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, FunctionPointerNullFail) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "Failed to identify nullptr.");
  int (*some_fn)() = nullptr;
  ASSERT_NOT_NULL(some_fn);
  TEST_CHECKPOINT();
}

class MyClassWithMethods {
 public:
  int MyMethod() const { return 0; }
};

TEST(ZxTestAssertionTest, MemberMethodFunctionNull) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to identify false.");
  int (MyClassWithMethods::*method)() const = &MyClassWithMethods::MyMethod;
  ASSERT_NOT_NULL(method);
  EXPECT_NOT_NULL(method);
  ASSERT_EQ(method, &MyClassWithMethods::MyMethod);
  ASSERT_NE(method, nullptr);
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, MemberMethodFunctionNullFail) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "Failed to identify false.");
  int (MyClassWithMethods::*method)() const = nullptr;
  EXPECT_EQ(method, &MyClassWithMethods::MyMethod);
  ASSERT_NOT_NULL(method);
  TEST_CHECKPOINT();
}

}  // namespace

class ConverToBoolExplicit {
 public:
  ConverToBoolExplicit(bool value) { value_ = value; }
  virtual ~ConverToBoolExplicit() {}

  explicit operator bool() const { return value_; }

 private:
  bool value_;
};

class ConverToBoolExplicitNotCopyable : public ConverToBoolExplicit {
 public:
  ConverToBoolExplicitNotCopyable(bool value) : ConverToBoolExplicit(value) {}
  ConverToBoolExplicitNotCopyable(const ConverToBoolExplicitNotCopyable&) = delete;
};

class ConverToBoolExplicitNotMoveable : public ConverToBoolExplicit {
 public:
  ConverToBoolExplicitNotMoveable(bool value) : ConverToBoolExplicit(value) {}
  ConverToBoolExplicitNotMoveable(const ConverToBoolExplicitNotMoveable&) = delete;
  ConverToBoolExplicitNotMoveable(ConverToBoolExplicitNotMoveable&&) = delete;
};

namespace {
TEST(ZxTestAssertionTest, CoerceNullPtrToBoolExplicitBase) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to identify false.");
  void* val = nullptr;
  ASSERT_FALSE(val);
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, CoerceExplicitTypeToBoolNonMoveable) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to identify false.");
  char b;
  char* val = &b;
  ASSERT_TRUE(val);
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, CoerceTypeToBoolExplicitBase) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to identify false.");
  ConverToBoolExplicit val(true);
  ASSERT_TRUE(val);
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, CoerceTypeToBoolExplicitNonCopyable) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to identify false.");
  ConverToBoolExplicitNotCopyable val(true);
  ASSERT_TRUE(val);
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, CoerceTypeToBoolExplicitNonMoveable) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to identify false.");
  ConverToBoolExplicitNotMoveable val(true);
  ASSERT_TRUE(val);
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, PromoteLiteralIntegersOnComp) {
  int32_t a = -1;
  int64_t b = 2;
  int16_t c = -1;
  int64_t d = 1;

  uint32_t e = 1;
  uint64_t f = 2;
  uint64_t g = 3;
  uint16_t h = 1;

  // Signed to wider ints.
  ASSERT_EQ(a, b);
  ASSERT_GE(b, a);
  ASSERT_LE(a, b);
  ASSERT_GT(b, c);
  ASSERT_LT(b, a);
  ASSERT_GT(b, d);

  // Signed comparison with literals.
  ASSERT_EQ(-1, a);
  ASSERT_EQ(1, d);
  ASSERT_LT(c, 3);
  ASSERT_GT(b, 1);
  ASSERT_GE(b, 2);

  // Unsigned to wider ints.
  ASSERT_EQ(e, h);
  ASSERT_GE(g, f);
  ASSERT_LE(f, g);
  ASSERT_GT(g, e);
  ASSERT_LT(h, f);

  // Unsigned comparison with literals.
  ASSERT_EQ(1, e);
  ASSERT_LT(f, 4);
  ASSERT_LE(f, 2);
  ASSERT_GT(g, 2);
  ASSERT_GE(g, 3);
}

TEST(ZxTestAssertionTest, PrintfLikeDescs) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "Failed to identify true.");
  int a = 1;
  EXPECT_FALSE(a, "Message ");
  EXPECT_FALSE(a, "One %d ", a);
  EXPECT_FALSE(a, "More than one %d %d.", a, a);
  EXPECT_FALSE(a, "More than one %d %d %d %d %d.", a, a, a, a, a);
  EXPECT_FALSE(a, "More than one %d %d %d %d %d %d %d %d %d %d %d %d %d %d.", a, a, a, a, a, a, a,
               a, a, a, a, a, a, a);
  TEST_CHECKPOINT();
}

int HasExpects() {
  EXPECT_EQ(1, 2);
  return 0;
}

TEST(ZxTestAssertionTest, NonVoidHelperTestNonFatalFailures) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "Failed to propagate assertion error.");
  ASSERT_NO_FATAL_FAILURES(HasExpects());
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertNoFailures) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "Failed to detect non fatal failure");
  ASSERT_NO_FAILURES(HasExpects());
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AddFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "Failed to detect non fatal failure");
  ADD_FAILURE("Something went wrong.");
  ASSERT_NO_FATAL_FAILURES();
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AddFatalFailure) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "Failed to detect fatal failure");
  ADD_FATAL_FAILURE("Something went wrong.");
  ASSERT_NO_FATAL_FAILURES();
  TEST_CHECKPOINT();
}

void AssertFail() {
  ASSERT_TRUE(false);
  return;
}

TEST(ZxTestAssertionTest, CurrentTestHasFailuresDetectsNonFatalFailures) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "Failed to detect failure");
  EXPECT_TRUE(false);
  ASSERT_TRUE(CURRENT_TEST_HAS_FAILURES());
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, CurrentTestHasFailuresDetectsFatalFailures) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "Failed to detect failure");
  AssertFail();
  ASSERT_TRUE(CURRENT_TEST_HAS_FAILURES());
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, CurrentTestHasFatalFailuresIgnoresNonFatalFailures) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "Failed to detect failure");
  EXPECT_TRUE(false);
  ASSERT_FALSE(CURRENT_TEST_HAS_FATAL_FAILURES());
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, CurrentTestHasFatalFailuresDetectsFatalFailures) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "Failed to detect failure");
  AssertFail();
  ASSERT_TRUE(CURRENT_TEST_HAS_FATAL_FAILURES());
  TEST_CHECKPOINT();
}

#ifdef __Fuchsia__
void Crash() { ZX_ASSERT(false); }

void Success() { ZX_ASSERT(true); }

TEST(ZxTestAssertionTest, AssertDeathWithCrashingLambdaStatement) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to detect crash");
  ASSERT_DEATH([]() { Crash(); }, "Crash was not raised.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertDeathWithCrashingStatement) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to detect crash");
  ASSERT_DEATH(&Crash, "Crash was not raised.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertDeathWithSuccessfulStatement) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "Failed to detect crash");
  ASSERT_DEATH(&Success, "Crash was not raised.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertNoDeathWithSuccessfullLambdaStatement) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to detect crash");
  ASSERT_NO_DEATH([]() { Success(); }, "Crash was raised.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertNoDeathWithSuccessfulStatement) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to detect crash");
  ASSERT_NO_DEATH(&Success, "Crash was raised.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionTest, AssertNoDeathWithCrashingStatement) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS, "Failed to detect crash");
  ASSERT_NO_DEATH(&Crash, "Crash was raised.");
  TEST_CHECKPOINT();
}

#endif

TEST(ZxTestAssertionTest, AssertBytesEqVla) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "Failed to check buffer eq.");
  volatile int len = 2;
  char a[len];
  const char* b = reinterpret_cast<const char*>(a);

  memset(a, 0, len);

  ASSERT_BYTES_EQ(a, b, len);
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertStatusSuccess) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "ASSERT/EXPECT_STATUS aborted test on success.");
  zx_status_t a = ZX_ERR_BAD_STATE;
  zx_status_t b = ZX_ERR_BAD_STATE;

  // Happy cases.
  EXPECT_STATUS(a, ZX_ERR_BAD_STATE, "EXPECT_STATUS identity failed.");
  EXPECT_STATUS(ZX_ERR_BAD_STATE, a, "EXPECT_STATUS identity failed.");
  ASSERT_STATUS(ZX_OK, ZX_OK, "ASSERT_STATUS identity failed.");
  EXPECT_STATUS(a, a, "EXPECT_STATUS identity failed.");
  ASSERT_STATUS(b, b, "ASSERT_STATUS identity failed.");
  ASSERT_STATUS(a, b, "ASSERT_STATUS identity failed.");
  // No failures
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertStatusFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_STATUS aborted execution.");
  zx_status_t a = ZX_ERR_INVALID_ARGS;
  zx_status_t b = ZX_ERR_BAD_STATE;

  EXPECT_STATUS(ZX_OK, ZX_ERR_INVALID_ARGS, "EXPECT_STATUS inequality detection succeeded.");
  EXPECT_STATUS(a, b, "EXPECT_STATUS inequality detection succeeded.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertStatusFailureFatal) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                   "ASSERT_STATUS did not abort test execution.");
  ASSERT_STATUS(ZX_OK, ZX_ERR_BAD_STATE, "ASSERT_STATUS inequality detection succeeded.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertNotStatusSuccess) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, NO_ERRORS, "EXPECT_NOT_STATUS aborted test execution.");
  zx_status_t a = ZX_ERR_BAD_STATE;
  zx_status_t b = ZX_ERR_INVALID_ARGS;

  // Happy cases.
  EXPECT_NOT_STATUS(ZX_OK, ZX_ERR_BAD_STATE, "EXPECT_NOT_STATUS inequality detection succeeded.");
  EXPECT_NOT_STATUS(a, b, "EXPECT_NOT_STATUS inequality detection succeeded.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertNotStatusFailure) {
  TEST_EXPECTATION(CHECKPOINT_REACHED, HAS_ERRORS, "EXPECT_NOT_STATUS aborted test execution.");
  zx_status_t a = ZX_OK;

  EXPECT_NOT_STATUS(ZX_ERR_BAD_STATE, ZX_ERR_BAD_STATE,
                    "EXPECT_NOT_STATUS equality detection succeeded.");
  EXPECT_NOT_STATUS(a, a, "EXPECT_NOT_STATUS equality detection succeeded.");
  TEST_CHECKPOINT();
}

TEST(ZxTestAssertionsTest, AssertNotStatusFailureFatal) {
  TEST_EXPECTATION(CHECKPOINT_NOT_REACHED, HAS_ERRORS,
                   "ASSERT_NOT_STATUS  did not abort test execution.");
  zx_status_t a = ZX_OK;
  zx_status_t b = ZX_OK;

  ASSERT_NOT_STATUS(a, b, "ASSERT_NOT_STATUS equality detection succeeded.");
  TEST_CHECKPOINT();
}

}  // namespace
