// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_ASSERT_STRSTR_H_
#define ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_ASSERT_STRSTR_H_

#include <string.h>

// This definition conflicts with the definition provided by unittest.h, so
// it is in a separate header file.
//
// TODO(fxb/51652): This header can be combined with error_test.h once all
// the fidl-compiler tests use zxtest.h.

// Assert that string1 contains string2 as a substring.
#define ASSERT_STR_STR(string1, string2) \
  ASSERT_NOT_NULL(strstr(string1, string2), "\"%s\" does not contain \"%s\"", string1, string2)

#endif  // ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_ASSERT_STRSTR_H_
