// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/thread_checker.h>

#include <thread>

#include <zxtest/zxtest.h>

namespace {

TEST(ThreadCheckerTest, SameThread) {
  fit::thread_checker checker;
  EXPECT_TRUE(checker.is_thread_valid());
}

TEST(ThreadCheckerTest, DifferentThreads) {
  fit::thread_checker checker1;
  EXPECT_TRUE(checker1.is_thread_valid());
  checker1.lock();
  checker1.unlock();

  std::thread thread([&checker1]() {
    fit::thread_checker checker2;
    EXPECT_TRUE(checker2.is_thread_valid());
    EXPECT_FALSE(checker1.is_thread_valid());
    checker2.lock();
    checker2.unlock();
  });
  thread.join();

  // Note: Without synchronization, we can't look at |checker2| from the main
  // thread.
}

}  // namespace
