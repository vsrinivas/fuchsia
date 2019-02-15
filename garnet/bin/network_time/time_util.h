// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETWORK_TIME_TIME_UTIL_H_
#define GARNET_BIN_NETWORK_TIME_TIME_UTIL_H_

#include <time.h>
#include <string>

#include "fuchsia/hardware/rtc/cpp/fidl.h"

namespace time_server {
std::string ToIso8601String(time_t epoch_seconds);
std::string ToIso8601String(const struct tm *tm);
const fuchsia::hardware::rtc::Time ToRtcTime(const struct tm *tm);
}  // namespace time_server

#endif  // GARNET_BIN_NETWORK_TIME_TIME_UTIL_H_
