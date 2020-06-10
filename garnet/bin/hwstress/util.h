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

// Create a 64-bit pattern by repeating the same 8-bit value 8 times.
inline uint64_t RepeatByte(uint8_t v) { return v * 0x0101'0101'0101'0101ul; }

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_UTIL_H_
