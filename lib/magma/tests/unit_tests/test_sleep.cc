// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/sleep.h"
#include "gtest/gtest.h"

class TestSleep {
public:
    void Msleep(uint32_t ms)
    {
        auto start = std::chrono::high_resolution_clock::now();
        magma::msleep(ms);
        auto end = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double, std::milli> elapsed = end - start;

        EXPECT_GE(elapsed.count(), ms);
        // Accept some delay due to scheduling, etc.
        EXPECT_LT(elapsed.count(), ms + 10);
    }
};

TEST(MagmaUtil, Sleep)
{
    TestSleep test;
    test.Msleep(1);
    test.Msleep(2);
    test.Msleep(10);
    test.Msleep(100);
    test.Msleep(1000);
}
