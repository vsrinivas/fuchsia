// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <pthread.h>

#include <cstdlib>

#include <zxtest/zxtest.h>

namespace {

struct ThreadArgs {
    pthread_barrier_t* barrier;
    std::atomic<int> result;
};

void* BarrierWait(void* arg) {
    auto args = reinterpret_cast<ThreadArgs*>(arg);
    args->result = pthread_barrier_wait(args->barrier);

    return nullptr;
}

constexpr int kNumThreads = 16;
constexpr int kNumIterations = 128;
constexpr pthread_barrierattr_t* kDefaultBarrierAttrs = nullptr;
constexpr pthread_attr_t* kDefaultPthreadAttrs = nullptr;
constexpr void** kNoRetValue = nullptr;

TEST(PThreadBarrierTest, SingleThreadWinsBarrierObject) {
    pthread_barrier_t barrier;
    ASSERT_EQ(pthread_barrier_init(&barrier, kDefaultBarrierAttrs, kNumThreads), 0);

    pthread_t threads[kNumThreads];
    ThreadArgs args[kNumThreads];

    for (int idx = 0; idx < kNumThreads; ++idx) {
        args[idx].barrier = &barrier;
        ASSERT_EQ(pthread_create(&threads[idx], kDefaultPthreadAttrs, &BarrierWait,
                  static_cast<void*>(&args[idx])), 0);
    }

    for (int idx = 0; idx < kNumThreads; ++idx) {
        ASSERT_EQ(pthread_join(threads[idx], kNoRetValue), 0);
    }

    int num_wins = 0;
    int num_zeros = 0;
    for (int idx = 0; idx < kNumThreads; ++idx) {
        int result = args[idx].result;
        if (result == PTHREAD_BARRIER_SERIAL_THREAD) {
            num_wins++;
        } else if (result == 0) {
            num_zeros++;
        } else {
            ASSERT_EQ(result, 0, "bad result for thread: %d result: %d\n", idx, result);
        }
    }
    ASSERT_EQ(num_wins, 1);
    ASSERT_EQ(num_zeros, kNumThreads - 1);
    ASSERT_EQ(pthread_barrier_destroy(&barrier), 0);
}

TEST(PThreadBarrierTest, SingleThreadWinsBarrierObjectResetsBetweenIterations) {
    pthread_barrier_t barrier;
    ASSERT_EQ(pthread_barrier_init(&barrier, kDefaultBarrierAttrs, kNumThreads), 0);

    for (int count = 0; count < kNumIterations; ++count) {
        pthread_t threads[kNumThreads];
        ThreadArgs args[kNumThreads];

        for (int idx = 0; idx < kNumThreads; ++idx) {
            args[idx].barrier = &barrier;
            ASSERT_EQ(pthread_create(&threads[idx], kDefaultPthreadAttrs, &BarrierWait,
                      static_cast<void*>(&args[idx])), 0);
        }

        for (int idx = 0; idx < kNumThreads; ++idx) {
            ASSERT_EQ(pthread_join(threads[idx], kNoRetValue), 0);
        }

        int num_wins = 0;
        int num_zeros = 0;
        for (int idx = 0; idx < kNumThreads; ++idx) {
            int result = args[idx].result;
            if (result == PTHREAD_BARRIER_SERIAL_THREAD) {
                num_wins++;
            } else if (result == 0) {
                num_zeros++;
            } else {
                ASSERT_EQ(result, 0, "bad result for thread: %d result: %d\n", idx, result);
            }
        }
        ASSERT_EQ(num_wins, 1);
        ASSERT_EQ(num_zeros, kNumThreads - 1);
    }
    ASSERT_EQ(pthread_barrier_destroy(&barrier), 0);
}

TEST(PThreadBarrierTest, InitWithNoThreadsReturnsInval) {
    pthread_barrier_t barrier;
    constexpr int kThreadCount = 0;
    ASSERT_EQ(pthread_barrier_init(&barrier, kDefaultBarrierAttrs, kThreadCount), EINVAL,
              "zero thread count should fail");
}

} // namespace
