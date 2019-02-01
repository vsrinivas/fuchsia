// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETWORK_TIME_TIME_UTIL_H
#define GARNET_BIN_NETWORK_TIME_TIME_UTIL_H

#include <time.h>
#include <string>

namespace time_server {
std::string ToIso8601String(time_t epoch_seconds);
std::string ToIso8601String(struct tm *tm);
}  // namespace time_server

#endif  // GARNET_BIN_NETWORK_TIME_TIME_UTIL_H
