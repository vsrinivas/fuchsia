// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <crypto/aead.h>
#include <crypto/cipher.h>
#include <crypto/hkdf.h>
#include <zircon/status.h>
#include <zircon/types.h>

// For expressions that return a zx_status_t, this macro will print the symbolic names when a
// mismatch is encountered.  Don't use this directly; use the wrappers below.
#define UT_ZX(lhs, rhs, ret)                                                                       \
    do {                                                                                           \
        UT_ASSERT_VALID_TEST_STATE;                                                                \
        zx_status_t _lhs_val = (lhs);                                                              \
        zx_status_t _rhs_val = (rhs);                                                              \
        if (_lhs_val != _rhs_val) {                                                                \
            UNITTEST_FAIL_TRACEF("%s returned %s; expected %s\n", #lhs,                            \
                                 zx_status_get_string(_lhs_val), zx_status_get_string(_rhs_val));  \
            current_test_info->all_ok = false;                                                     \
            ret;                                                                                   \
        }                                                                                          \
    } while (0)

// Wrappers for UT_ZX.
#define EXPECT_ZX(expr, status) UT_ZX(expr, status, DONOT_RET)
#define ASSERT_ZX(expr, status) UT_ZX(expr, status, RET_FALSE)
#define EXPECT_OK(expr) UT_ZX(expr, ZX_OK, DONOT_RET)
#define ASSERT_OK(expr) UT_ZX(expr, ZX_OK, RET_FALSE)

// Value-parameterized tests: Consumers of this file can define an 'EACH_PARAM' macro as follows:
//   #define EACH_PARAM(OP, Test)
//   OP(Test, Class, Param1)
//   OP(Test, Class, Param2)
//   ...
//   OP(Test, Class, ParamN)
// where |Param1| corresponds to an enum constant |Class::kParam1|, etc.
//
// Consumers can then use the following macros to automatically define and run tests for each
// parameter:
//   bool TestSomething(Param param) {
//       BEGIN_TEST;
//       ...
//       END_TEST;
//   }
//   DEFINE_EACH(TestSomething)
//   ...
//   BEGIN_TEST_CASE(SomeTest)
//   RUN_EACH(TestSomething)
//   END_TEST_CASE(SomeTest)
#define DEFINE_TEST_PARAM(Test, Class, Param) bool Test ## _ ## Param(void) { return Test(Class::k ## Param); }
#define RUN_TEST_PARAM(Test, Class, Param) RUN_TEST(Test ## _ ## Param)
#define DEFINE_EACH(Test) EACH_PARAM(DEFINE_TEST_PARAM, Test)
#define RUN_EACH(Test) EACH_PARAM(RUN_TEST_PARAM, Test)

namespace crypto {
namespace testing {

// Returns true if and only if the |len| bytes starting at offset |off| in |buf| are all equal to
// |val|.
bool AllEqual(const void* buf, uint8_t val, zx_off_t off, size_t len);

// Returns a pointer to a freshly allocated buffer of |kBlockSize| zeros.
fbl::unique_ptr<uint8_t[]> MakeZeroPage();

// Returns a pointer to a freshly allocated buffer of |kBlockSize| random bytes.
fbl::unique_ptr<uint8_t[]> MakeRandPage();

// Resizes |out| and sets its contents to match the given |hex| string.
zx_status_t HexToBytes(const char* hex, Bytes* out);

// Fills the given |key| and |iv| with as much random data as indicated by |Cipher::GetKeyLen| and
// |Cipher::GetIVLen| for the given |cipher|. |iv| may be null.
zx_status_t GenerateKeyMaterial(Cipher::Algorithm cipher, Bytes* key, Bytes* iv);

// Fills the given |key|, |iv| with as much random data as indicated by |AEAD::GetKeyLen| and
//|AEAD::GetIVLen| for the given |aead|. |iv| may be null.
zx_status_t GenerateKeyMaterial(AEAD::Algorithm aead, Bytes* key, Bytes* iv);

} // namespace testing
} // namespace crypto
