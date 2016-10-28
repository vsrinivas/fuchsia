// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <thread>

#include "magma_util/platform/platform_futex.h"
#include "magma_util/sleep.h"
#include "gtest/gtest.h"

static inline uint64_t timeout_from_ms(uint64_t timeout_ms) { return timeout_ms * 1000000ull; }

class TestPlatformFutex {
public:
    static void Test()
    {
        value = 10;

        magma::PlatformFutex::WaitResult result;
        EXPECT_TRUE(magma::PlatformFutex::WaitForever(&value, value + 1, &result));
        EXPECT_EQ(result, magma::PlatformFutex::RETRY);

        auto start = std::chrono::high_resolution_clock::now();

        EXPECT_TRUE(
            magma::PlatformFutex::Wait(&value, value, timeout_from_ms(timeout_ms), &result));
        EXPECT_EQ(result, magma::PlatformFutex::TIMED_OUT);

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;

        EXPECT_GT(elapsed.count(), (uint32_t)timeout_ms);

        uint32_t starting_value = value;

        std::thread thread([]() {
            magma::PlatformFutex::WaitResult result;
            EXPECT_TRUE(magma::PlatformFutex::WaitForever(&value, value, &result));
            EXPECT_EQ(result, magma::PlatformFutex::AWOKE);
            value++;
        });

        constexpr uint32_t max_retry = 100;
        uint32_t retry = 0;

        for (; retry < max_retry; retry++) {
            magma::PlatformFutex::Wake(&value, 1);
            if (value != starting_value) {
                thread.join();
                break;
            }
            magma::msleep(1);
        }

        EXPECT_LT(retry, max_retry);
    }

    static constexpr uint32_t timeout_ms = 100;
    static uint32_t value;
};

uint32_t TestPlatformFutex::value;

TEST(PlatformFutex, Test) { TestPlatformFutex::Test(); }
