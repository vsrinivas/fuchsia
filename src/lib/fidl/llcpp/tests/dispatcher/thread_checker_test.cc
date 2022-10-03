// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/wire/internal/synchronization_checker.h>

#include <thread>

#include <zxtest/zxtest.h>

#include "src/lib/fidl/llcpp/tests/dispatcher/lsan_disabler.h"

#ifndef NDEBUG

// |SynchronizationChecker| should check that it is always used from the same thread in
// debug builds.
TEST(SynchronizationChecker, CheckInDebug) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  fidl::internal::DebugOnlySynchronizationChecker checker{
      loop.dispatcher(), fidl::internal::ThreadingPolicy::kCreateAndTeardownFromDispatcherThread};
  std::thread thread([&] {
    ASSERT_DEATH([&] {
      fidl_testing::RunWithLsanDisabled([&] { fidl::internal::ScopedThreadGuard guard(checker); });
    });
  });
  thread.join();
}

// It is possible to configure whether to skip the check.
TEST(SynchronizationChecker, SkipCheckUsingPolicy) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  fidl::internal::DebugOnlySynchronizationChecker checker{
      loop.dispatcher(), fidl::internal::ThreadingPolicy::kCreateAndTeardownFromAnyThread};
  std::thread thread(
      [&] { ASSERT_NO_DEATH([&] { fidl::internal::ScopedThreadGuard guard(checker); }); });
  thread.join();
}

#else

// |SynchronizationChecker| should not perform any assertions in release builds.
TEST(SynchronizationChecker, NoCheckInRelease) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  fidl::internal::DebugOnlySynchronizationChecker checker{
      loop.dispatcher(), fidl::internal::ThreadingPolicy::kCreateAndTeardownFromDispatcherThread};
  std::thread thread(
      [&] { ASSERT_NO_DEATH([&] { fidl::internal::ScopedThreadGuard guard(checker); }); });
  thread.join();
}

#endif  // NDEBUG
