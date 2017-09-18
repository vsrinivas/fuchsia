// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>
#include <stdio.h>

// TODO(dje): KISS until things finalize.
// "EXPECT" versions of these macros don't invoke "return".
// Could rename these to ASSERT, but the c++ tests all use EXPECT:
// in the interests of maintaining consistency until things finalize,
// we continue to use EXPECT.
#define C_EXPECT_WORKER(a, b, cmp) \
  do { \
    __typeof__(a) __c_expect_a = (a); \
    __typeof__(b) __c_expect_b = (b); \
    if (!((__c_expect_a) cmp (__c_expect_b))) { \
      printf("[FAILED]: %s:%d: a" #cmp "b: a=\"%s\"(%" PRId64 ") b=\"%s\"(%" PRId64 ")\n", \
             __PRETTY_FUNCTION__, __LINE__, \
             #a, (uint64_t) (__c_expect_a), #b, (uint64_t) (__c_expect_b)); \
      return false; \
    } \
  } while (0)

#define C_EXPECT_EQ(a, b) C_EXPECT_WORKER((a), (b), ==)
#define C_EXPECT_NE(a, b) C_EXPECT_WORKER((a), (b), !=)

#define C_EXPECT_TRUE(a) C_EXPECT_WORKER((a), true, ==)
#define C_EXPECT_FALSE(a) C_EXPECT_WORKER((a), false, ==)
