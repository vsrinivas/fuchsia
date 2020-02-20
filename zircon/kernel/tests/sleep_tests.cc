// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <platform.h>
#include <stdio.h>
#include <zircon/types.h>

#include <kernel/thread.h>

#include "tests.h"

// Tests that thread_sleep and current_time() are consistent.
static int thread_sleep_test(void) {
  int early = 0;
  for (int i = 0; i < 5; i++) {
    zx_time_t now = current_time();
    CurrentThread::SleepRelative(ZX_MSEC(500));
    zx_duration_t actual_delay = current_time() - now;
    if (actual_delay < ZX_MSEC(500)) {
      early = 1;
      printf("CurrentThread::SleepRelative(ZX_MSEC(500)) returned after %" PRIi64 " ns\n",
             actual_delay);
    }
  }
  return early;
}

int sleep_tests(int, const cmd_args*, uint32_t) {
  thread_sleep_test();
  return 0;
}
