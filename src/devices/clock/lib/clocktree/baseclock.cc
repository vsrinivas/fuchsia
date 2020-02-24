// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <zircon/assert.h>
#include <fbl/auto_lock.h>

#include "baseclock.h"
#include "clocktree.h"
#include "types.h"

namespace clk {

zx_status_t BaseClock::IsEnabled(bool* out) {
  zx_status_t st = this->IsHwEnabled(out);

  if (st == ZX_ERR_NOT_SUPPORTED) {
    *out = this->EnableCount() > 0;
    return ZX_OK;
  }

  return st;
}

}  // namespace clk
