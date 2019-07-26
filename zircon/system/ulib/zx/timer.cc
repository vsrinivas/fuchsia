// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/timer.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t timer::create(uint32_t options, zx_clock_t clock_id, timer* result) {
  return zx_timer_create(options, clock_id, result->reset_and_get_address());
}

}  // namespace zx
