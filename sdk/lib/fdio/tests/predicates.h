// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_TESTS_PREDICATES_H_
#define LIB_FDIO_TESTS_PREDICATES_H_

#define ASSERT_ERRNO(val) ASSERT_EQ(errno, val, "%s", strerror(errno))
#define EXPECT_ERRNO(val) EXPECT_EQ(errno, val, "%s", strerror(errno))

#define ASSERT_SUCCESS(val) ASSERT_EQ(val, 0, "%s", strerror(errno))
#define EXPECT_SUCCESS(val) EXPECT_EQ(val, 0, "%s", strerror(errno))

#endif  // LIB_FDIO_TESTS_PREDICATES_H_
