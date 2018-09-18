// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_timer.h"

#include <zircon/system/public/zircon/assert.h>
#include <utility>

namespace wlan {

zx_status_t TestTimer::SetTimerImpl(zx::time deadline) {
    return ZX_OK;
}

zx_status_t TestTimer::CancelTimerImpl() {
    return ZX_OK;
}

}  // namespace wlan
