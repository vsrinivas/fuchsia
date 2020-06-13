// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "event.h"

zx_status_t Event::Wait(zx::duration timeout) {
  zx::time deadline =
      (timeout == zx::duration::infinite()) ? zx::time::infinite() : zx::deadline_after(timeout);

  while (signaled_.load(fbl::memory_order_relaxed) == 0) {
    zx_status_t res = zx_futex_wait(&signaled_, 0, ZX_HANDLE_INVALID, deadline.get());
    if ((res != ZX_OK) && (res != ZX_ERR_BAD_STATE)) {
      return res;
    }
  }

  return ZX_OK;
}

void Event::Signal() {
  if (signaled_.load(fbl::memory_order_relaxed) == 0) {
    signaled_.store(1, fbl::memory_order_relaxed);
    zx_futex_wake(&signaled_, UINT32_MAX);
  }
}

void Event::Reset() { signaled_.store(0, fbl::memory_order_relaxed); }
