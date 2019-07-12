// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <thread>

#include "gtest/gtest.h"
#include "magma_util/dlog.h"
#include "platform_semaphore.h"

namespace {

class TestSemaphore {
 public:
  static void Test() {
    std::shared_ptr<magma::PlatformSemaphore> sem = magma::PlatformSemaphore::Create();
    std::unique_ptr<std::thread> thread;

    // Verify timeout
    thread.reset(new std::thread([sem] {
      DLOG("Waiting for semaphore");
      EXPECT_EQ(sem->Wait(100), MAGMA_STATUS_TIMED_OUT);
      DLOG("Semaphore wait returned");
    }));
    thread->join();

    // Verify return before timeout
    thread.reset(new std::thread([sem] {
      DLOG("Waiting for semaphore");
      EXPECT_TRUE(sem->Wait(100));
      DLOG("Semaphore wait returned");
    }));
    sem->Signal();
    thread->join();

    // Verify autoreset - should timeout again
    thread.reset(new std::thread([sem] {
      DLOG("Waiting for semaphore");
      EXPECT_EQ(sem->Wait(100), MAGMA_STATUS_TIMED_OUT);
      DLOG("Semaphore wait returned");
    }));
    thread->join();

    // Verify wait with no timeout
    thread.reset(new std::thread([sem] {
      DLOG("Waiting for semaphore");
      EXPECT_TRUE(sem->Wait());
      DLOG("Semaphore wait returned");
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sem->Signal();
    thread->join();

    // Verify Reset - should timeout.
    sem->Signal();
    sem->Reset();
    thread.reset(new std::thread([sem] {
      DLOG("Waiting for semaphore");
      EXPECT_EQ(sem->Wait(100), MAGMA_STATUS_TIMED_OUT);
      DLOG("Semaphore wait returned");
    }));
    thread->join();
  }
};

}  // namespace

TEST(PlatformSemaphore, Test) { TestSemaphore::Test(); }
