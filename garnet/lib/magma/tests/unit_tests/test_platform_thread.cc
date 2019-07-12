// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <thread>

#include "gtest/gtest.h"
#include "platform_thread.h"

class TestPlatformThread {
 public:
  static void ThreadFunc(magma::PlatformThreadId* thread_id) {
    EXPECT_FALSE(thread_id->IsCurrent());

    std::string name("thread name");
    magma::PlatformThreadHelper::SetCurrentThreadName(name);
    EXPECT_EQ(name, magma::PlatformThreadHelper::GetCurrentThreadName());
  }

  static void Test() {
    magma::PlatformThreadId thread_id;
    EXPECT_TRUE(thread_id.IsCurrent());

    std::thread thread(ThreadFunc, &thread_id);
    thread.join();
  }
};

TEST(PlatformThread, Test) { TestPlatformThread::Test(); }

TEST(PlatformProcess, Test) {
  // Exact name might depend on platform.
  EXPECT_NE(std::string(""), magma::PlatformProcessHelper::GetCurrentProcessName());
  EXPECT_NE(0u, magma::PlatformProcessHelper::GetCurrentProcessId());
}
