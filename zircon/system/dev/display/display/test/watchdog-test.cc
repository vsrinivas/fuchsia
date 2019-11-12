// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../watchdog.h"

#include <lib/async-testing/test_loop.h>
#include <lib/async/default.h>
#include <lib/async/cpp/task.h>

#include <zxtest/zxtest.h>

#include <threads.h>

namespace display {

class WatchdogTest : public zxtest::Test {};

TEST_F(WatchdogTest, CanResetAndStop) {
  async::TestLoop loop;
  thrd_t wd_thread;
  Watchdog wd;
  wd.Init(loop.dispatcher(), zx::duration(ZX_MSEC(10)), "should not fire");

  auto thread_body = [](void* arg) { return static_cast<Watchdog*>(arg)->Run(); };
  async::PostDelayedTask(loop.dispatcher(), [&wd] { wd.Reset(); }, zx::duration(ZX_MSEC(9)));
  ASSERT_OK(thrd_create_with_name(&wd_thread, thread_body, &wd, "watchdog"));
  async::PostDelayedTask(loop.dispatcher(), [&wd] { wd.Stop(); }, zx::duration(ZX_MSEC(11)));

  loop.RunFor(zx::duration(ZX_MSEC(25)));
  thrd_join(wd_thread, nullptr);
}

}  // namespace display
