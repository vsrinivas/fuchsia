// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_ZIRCON_TESTING_MUTEX_PI_EXERCISER_UTILS_H_
#define SRC_ZIRCON_TESTING_MUTEX_PI_EXERCISER_UTILS_H_

#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

template <typename Callable>
zx_status_t WaitFor(const Callable& check, zx::duration timeout) {
  zx::time deadline = zx::deadline_after(timeout);
  while (true) {
    if (check()) {
      return ZX_OK;
    }

    if (zx::clock::get_monotonic() > deadline) {
      return ZX_ERR_TIMED_OUT;
    }

    zx::nanosleep(zx::deadline_after(zx::msec(1)));
  }
}

#endif  // SRC_ZIRCON_TESTING_MUTEX_PI_EXERCISER_UTILS_H_
