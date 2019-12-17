// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <zircon/utc.h>

#include <atomic>

static_assert(sizeof(zx_handle_t) == 4, "Zircon handles must be 32 bits");
static std::atomic<zx_handle_t> g_zx_utc_reference_handle{ZX_HANDLE_INVALID};

zx_handle_t _zx_utc_reference_get(void) { return g_zx_utc_reference_handle.load(); }

__typeof(zx_utc_reference_get) zx_utc_reference_get
    __attribute__((weak, alias("_zx_utc_reference_get")));

zx_status_t _zx_utc_reference_swap(zx_handle_t new_utc_reference,
                                   zx_handle_t* prev_utc_reference_out) {
  // If the user is not disabling the UTC clock entirely, validate the handle
  // that they gave to us before proceeding.
  if (new_utc_reference != ZX_HANDLE_INVALID) {
    zx_time_t new_clock_now;
    zx_status_t res = _zx_clock_read(new_utc_reference, &new_clock_now);

    if (res != ZX_OK) {
      _zx_handle_close(new_utc_reference);
      return res;
    }
  }

  // Things check out.  Go ahead and swap the clocks.
  *prev_utc_reference_out = g_zx_utc_reference_handle.exchange(new_utc_reference);
  return ZX_OK;
}

__typeof(zx_utc_reference_swap) zx_utc_reference_swap
    __attribute__((weak, alias("_zx_utc_reference_swap")));
