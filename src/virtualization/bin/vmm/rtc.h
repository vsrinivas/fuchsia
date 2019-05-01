// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_RTC_H_
#define SRC_VIRTUALIZATION_BIN_VMM_RTC_H_

#include <time.h>
#include <zircon/types.h>

// Returns the current time in seconds.
static inline time_t rtc_time() {
  return zx_clock_get(ZX_CLOCK_UTC) / ZX_SEC(1);
}

#endif  // SRC_VIRTUALIZATION_BIN_VMM_RTC_H_
