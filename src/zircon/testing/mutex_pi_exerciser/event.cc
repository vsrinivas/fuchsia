// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "event.h"

#include <lib/stdcompat/atomic.h>

zx_status_t Event::Wait(zx::duration timeout) {
  zx::time deadline =
      (timeout == zx::duration::infinite()) ? zx::time::infinite() : zx::deadline_after(timeout);

  while (cpp20::atomic_ref<zx_futex_t>(signaled_).load(std::memory_order_relaxed) == 0) {
    zx_status_t res = zx_futex_wait(&signaled_, 0, ZX_HANDLE_INVALID, deadline.get());
    if ((res != ZX_OK) && (res != ZX_ERR_BAD_STATE)) {
      return res;
    }
  }

  return ZX_OK;
}

void Event::Signal() {
  cpp20::atomic_ref<zx_futex_t> signal_ref(signaled_);
  if (signal_ref.load(std::memory_order_relaxed) == 0) {
    signal_ref.store(1, std::memory_order_relaxed);
    zx_futex_wake(&signaled_, UINT32_MAX);
  }
}

void Event::Reset() {
  cpp20::atomic_ref<zx_futex_t>(signaled_).store(0, std::memory_order_relaxed);
}
