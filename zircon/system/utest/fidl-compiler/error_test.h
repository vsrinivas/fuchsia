// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_ERROR_TEST_H_
#define ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_ERROR_TEST_H_

#define ASSERT_ERR(actual_err, err_def, ...) \
  ASSERT_STR_EQ(actual_err->err.msg.data(), err_def.msg.data(), ##__VA_ARGS__)

#endif  // ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_ERROR_TEST_H_
