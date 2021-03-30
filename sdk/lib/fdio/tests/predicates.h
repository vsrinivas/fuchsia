// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_TESTS_PREDICATES_H_
#define LIB_FDIO_TESTS_PREDICATES_H_

#define ASSERT_ERRNO(val) ASSERT_EQ(errno, val, "%s", strerror(errno))
#define EXPECT_ERRNO(val) EXPECT_EQ(errno, val, "%s", strerror(errno))

#define ASSERT_SUCCESS(val) ASSERT_EQ(val, 0, "%s", strerror(errno))
#define EXPECT_SUCCESS(val) EXPECT_EQ(val, 0, "%s", strerror(errno))

// Sugar to eliminate superficial differences between zxtest and gtest.

#define ASSERT_STREQ(val1, val2, ...) ASSERT_STR_EQ(val2, val1, #__VA_ARGS__)
#define EXPECT_STREQ(val1, val2, ...) EXPECT_STR_EQ(val2, val1, #__VA_ARGS__)

#define ASSERT_NO_FATAL_FAILURE(statement, ...) ASSERT_NO_FATAL_FAILURES(statement, #__VA_ARGS__)

#endif  // LIB_FDIO_TESTS_PREDICATES_H_
