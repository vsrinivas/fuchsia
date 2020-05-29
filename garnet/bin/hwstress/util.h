// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_UTIL_H_
#define GARNET_BIN_HWSTRESS_UTIL_H_

#include <lib/zx/time.h>

#include <string>

namespace hwstress {

// Convert double representing a number of seconds to a zx::duration.
zx::duration SecsToDuration(double secs);

// Convert zx::duration to double representing the number of seconds.
double DurationToSecs(zx::duration d);

// Represent a double as a hexadecimal constant.
std::string DoubleAsHex(double v);

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_UTIL_H_
