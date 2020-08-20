// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_RTC_H_
#define SRC_VIRTUALIZATION_BIN_VMM_RTC_H_

#include <ctime>

// Returns the current time in seconds.
static inline time_t rtc_time() { return std::time(nullptr); }

#endif  // SRC_VIRTUALIZATION_BIN_VMM_RTC_H_
