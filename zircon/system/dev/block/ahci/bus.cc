// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bus.h"

#include <lib/zx/clock.h>
#include <unistd.h>

namespace ahci {

zx_status_t Bus::WaitForClear(size_t offset, uint32_t mask, zx::duration timeout) {
  int i = 0;
  zx::time deadline = zx::clock::get_monotonic() + timeout;
  do {
    uint32_t val;
    zx_status_t status = RegRead(offset, &val);
    if (status != ZX_OK) {
      return status;
    }
    if (!(val & mask))
      return ZX_OK;
    usleep(10 * 1000);
    i++;
  } while (zx::clock::get_monotonic() < deadline);
  return ZX_ERR_TIMED_OUT;
}

zx_status_t Bus::WaitForSet(size_t offset, uint32_t mask, zx::duration timeout) {
  int i = 0;
  zx::time deadline = zx::clock::get_monotonic() + timeout;
  do {
    uint32_t val;
    zx_status_t status = RegRead(offset, &val);
    if (status != ZX_OK) {
      return status;
    }
    if (val & mask)
      return ZX_OK;
    usleep(10 * 1000);
    i++;
  } while (zx::clock::get_monotonic() < deadline);
  return ZX_ERR_TIMED_OUT;
}

}  // namespace ahci
