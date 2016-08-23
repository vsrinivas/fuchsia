// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/mutex.h"
#include "gtest/gtest.h"

TEST(MagmaUtil, GetPow2)
{
    uint64_t pow2;

    EXPECT_FALSE(magma::get_pow2(0, &pow2));
    EXPECT_FALSE(magma::get_pow2(3, &pow2));

    EXPECT_TRUE(magma::get_pow2(1, &pow2));
    EXPECT_EQ(pow2, 0ul);

    EXPECT_TRUE(magma::get_pow2(2, &pow2));
    EXPECT_EQ(pow2, 1ul);

    EXPECT_TRUE(magma::get_pow2(4, &pow2));
    EXPECT_EQ(pow2, 2ul);

    EXPECT_TRUE(magma::get_pow2(8, &pow2));
    EXPECT_EQ(pow2, 3ul);

    EXPECT_TRUE(magma::get_pow2(16, &pow2));
    EXPECT_EQ(pow2, 4ul);

    EXPECT_TRUE(magma::get_pow2(PAGE_SIZE, &pow2));
    EXPECT_EQ(pow2, 12ul);
}
