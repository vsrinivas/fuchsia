// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <time.h>
#include <zircon/assert.h>

void test_monotonics() {
  // The test strategy here is limited, as we do not have a straightforward
  // mechanism with which to modify the underlying syscall behavior. We switch
  // back and forward between calling clock_gettime with CLOCK_MONOTONIC and
  // CLOCK_BOOTTIME, and assert their relative monotonicity. This test ensures
  // that these calls succeed, and that time is at least frozen, if not
  // increasing in a monotonic fashion, with repect to both clock ids.

  struct timespec ts;

  int which = 0;
  for (int i = 0; i < 100; i++) {
    struct timespec last = ts;

    switch (which) {
    case 0:
      ZX_ASSERT(0 == clock_gettime(CLOCK_MONOTONIC, &ts));
      break;
    case 1:
      ZX_ASSERT(0 == clock_gettime(CLOCK_BOOTTIME, &ts));
      break;
    case 2:
      ZX_ASSERT(0 == clock_gettime(CLOCK_MONOTONIC_RAW, &ts));
      break;
    }

    if (ts.tv_sec == last.tv_sec) {
      ZX_ASSERT_MSG(
        ts.tv_nsec >= last.tv_nsec,
        "clock_gettime(CLOCK_{MONOTONIC,BOOTTIME}): %ld < %ld",
        ts.tv_nsec, last.tv_nsec
      );
    } else {
      ZX_ASSERT_MSG(
        ts.tv_sec >= last.tv_sec,
        "clock_gettime(CLOCK_{MONOTONIC,BOOTTIME}): %ld (current) < %ld (last)",
        ts.tv_sec, last.tv_sec
      );
    }

    if (++which % 3 == 0) {
      which = 0;
    }
  }
}

int main() {
  test_monotonics();
  return 0;
}
