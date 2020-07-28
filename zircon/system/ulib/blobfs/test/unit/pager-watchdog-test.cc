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
  auto watchdog_or = PagerWatchdog::Create(kDeadline);
  ASSERT_OK(watchdog_or.status_value());

  bool called = false;
  watchdog_or->SetCallback([&]() { called = true; });
  watchdog_or->RunUntilIdle();
  ASSERT_FALSE(called);
}

TEST(PagerWatchdogTest, FiresOnDeadlineExceeded) {
  constexpr zx::duration kShortDeadline = zx::msec(1);
  auto watchdog_or = PagerWatchdog::Create(kShortDeadline);
  ASSERT_OK(watchdog_or.status_value());

  std::mutex m;
  std::condition_variable cv;
  bool called = false;
  watchdog_or->SetCallback([&]() {
    std::unique_lock<std::mutex> l(m);
    called = true;
    cv.notify_all();
  });

  std::unique_lock<std::mutex> l(m);
  PagerWatchdog::ArmToken token = watchdog_or->Arm();
  cv.wait(l, [&called]() { return called; });
}

TEST(PagerWatchdogTest, FiresOnDeadlineExceeded_MultipleTokens) {
  constexpr zx::duration kShortDeadline = zx::msec(1);
  auto watchdog_or = PagerWatchdog::Create(kShortDeadline);
  ASSERT_OK(watchdog_or.status_value());

  std::mutex m;
  std::condition_variable cv;
  int calls = 0;
  watchdog_or->SetCallback([&]() {
    std::unique_lock<std::mutex> l(m);
    calls++;
    cv.notify_all();
  });

  std::unique_lock<std::mutex> l(m);
  PagerWatchdog::ArmToken token1 = watchdog_or->Arm();
  PagerWatchdog::ArmToken token2 = watchdog_or->Arm();
  cv.wait(l, [&calls]() { return calls == 2; });
}

TEST(PagerWatchdogTest, DoesNotFireIfDisarmed) {
  constexpr zx::duration kLongDeadline = zx::sec(60);
  auto watchdog_or = PagerWatchdog::Create(kLongDeadline);
  ASSERT_OK(watchdog_or.status_value());

  bool called = false;
  watchdog_or->SetCallback([&]() { called = true; });

  {
    PagerWatchdog::ArmToken token = watchdog_or->Arm();
  }
  watchdog_or->RunUntilIdle();
  ASSERT_FALSE(called);
}

}  // namespace
}  // namespace pager
}  // namespace blobfs
