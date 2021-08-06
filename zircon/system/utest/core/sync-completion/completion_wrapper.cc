// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/cpp/completion.h>
#include <lib/zx/time.h>

#include <thread>

#include <zxtest/zxtest.h>

// These tests are meant as simple smoke tests for the C++ wrapper.
// The comprehensive tests for |sync_completion_t| are defined in
// completion.cc.
namespace {

TEST(CompletionWrapper, Wait) {
  sync::Completion completion;
  std::thread wait_thread([&] { completion.Wait(); });
  completion.Signal();
  wait_thread.join();
}

TEST(CompletionWrapper, WaitDurationTimeout) {
  sync::Completion completion;
  std::thread wait_thread(
      [&] { ASSERT_STATUS(ZX_ERR_TIMED_OUT, completion.Wait(zx::duration::infinite_past())); });
  wait_thread.join();
}

TEST(CompletionWrapper, WaitDuration) {
  sync::Completion completion;
  completion.Signal();
  std::thread wait_thread([&] { ASSERT_OK(completion.Wait(zx::duration::infinite_past())); });
  wait_thread.join();
}

TEST(CompletionWrapper, WaitDeadlineTimeout) {
  sync::Completion completion;
  std::thread wait_thread(
      [&] { ASSERT_STATUS(ZX_ERR_TIMED_OUT, completion.Wait(zx::time::infinite_past())); });
  wait_thread.join();
}

TEST(CompletionWrapper, WaitDeadline) {
  sync::Completion completion;
  completion.Signal();
  std::thread wait_thread([&] { ASSERT_OK(completion.Wait(zx::time::infinite_past())); });
  wait_thread.join();
}

TEST(CompletionWrapper, signaled) {
  sync::Completion completion;
  EXPECT_FALSE(completion.signaled());
  completion.Signal();
  EXPECT_TRUE(completion.signaled());
  completion.Reset();
  EXPECT_FALSE(completion.signaled());
}

}  // namespace
