// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

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

namespace crypto {
namespace testing {

// Returns true if and only if the |len| bytes starting at offset |off| in |buf| are all equal to
// |val|.
bool AllEqual(const void* buf, uint8_t val, zx_off_t off, size_t len);

// Returns a pointer to a freshly allocated buffer of |kBlockSize| zeros.
fbl::unique_ptr<uint8_t[]> MakeZeroPage();

// Returns a pointer to a freshly allocated buffer of |kBlockSize| random bytes.
fbl::unique_ptr<uint8_t[]> MakeRandPage();

} // namespace testing
} // namespace crypto
