// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pager/pager-watchdog.h"

#include <condition_variable>
#include <mutex>

#include <zxtest/zxtest.h>

namespace blobfs {
namespace pager {
namespace {

TEST(PagerWatchdogTest, NotArmedByDefault) {
  constexpr zx::duration kDeadline = zx::msec(1);
  PagerWatchdog watchdog(kDeadline);

  bool called = false;
  watchdog.SetCallback([&](int count) { called = true; });
  watchdog.RunUntilIdle();
  ASSERT_FALSE(called);
}

TEST(PagerWatchdogTest, FiresOnDeadlineExceeded) {
  constexpr zx::duration kShortDeadline = zx::msec(1);
  PagerWatchdog watchdog(kShortDeadline);

  std::mutex m;
  std::condition_variable cv;
  bool called = false;
  watchdog.SetCallback([&](int count) {
    std::unique_lock<std::mutex> l(m);
    called = true;
    cv.notify_all();
  });

  std::unique_lock<std::mutex> l(m);
  PagerWatchdog::ArmToken token = watchdog.Arm();
  cv.wait(l, [&called]() { return called; });
}

TEST(PagerWatchdogTest, DoesNotFireIfDisarmed) {
  constexpr zx::duration kLongDeadline = zx::sec(60);
  PagerWatchdog watchdog(kLongDeadline);

  bool called = false;
  watchdog.SetCallback([&](int count) { called = true; });

  { PagerWatchdog::ArmToken token = watchdog.Arm(); }
  watchdog.RunUntilIdle();
  ASSERT_FALSE(called);
}

TEST(PagerWatchdogDeathTest, AssertsWithMultipleTokens) {
  ASSERT_DEATH(([]() {
    static auto* watchdog = new PagerWatchdog(zx::sec(1));
    PagerWatchdog::ArmToken tokens[] = {watchdog->Arm(), watchdog->Arm()};
  }));
}

}  // namespace
}  // namespace pager
}  // namespace blobfs
