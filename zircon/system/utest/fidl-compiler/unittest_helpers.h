// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_UNITTEST_HELPERS_H_
#define ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_UNITTEST_HELPERS_H_

#include <unittest/unittest.h>

#define ASSERT_STRING_EQ(lhs, rhs, ...)     \
    ASSERT_STR_EQ(std::string(lhs).c_str(), \
                  std::string(rhs).c_str(), ##__VA_ARGS__)

#define ASSERT_NULLPTR(value, ...) ASSERT_EQ(value, nullptr, ##__VA_ARGS__)

#define ASSERT_NOT_NULLPTR(value, ...) ASSERT_NE(value, nullptr, ##__VA_ARGS__)

#define EXPECT_STRING_EQ(lhs, rhs, ...)     \
    EXPECT_STR_EQ(std::string(lhs).c_str(), \
                  std::string(rhs).c_str(), ##__VA_ARGS__)

#define EXPECT_NULLPTR(value, ...) EXPECT_EQ(value, nullptr, ##__VA_ARGS__)

#define EXPECT_NOT_NULLPTR(value, ...) EXPECT_NE(value, nullptr, ##__VA_ARGS__)

#endif // ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_UNITTEST_HELPERS_H_
