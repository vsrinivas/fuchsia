// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <lib/zx/clock.h>
#include <lib/zx/time.h>

#include <cstdio>
#include <string>

namespace hwstress {

zx::duration SecsToDuration(double secs) {
  return zx::nsec(static_cast<int64_t>(secs * 1'000'000'000.0));
}

double DurationToSecs(zx::duration d) { return static_cast<double>(d.to_nsecs()) / 1'000'000'000.0; }

std::string DoubleAsHex(double v) {
  // Standards-compliant way to convert a double to a long. The compiler will
  // just turn this into a register move.
  uint64_t n;
  static_assert(sizeof(uint64_t) == sizeof(double));
  memcpy(&n, &v, sizeof(double));

  // Print out the long as a string.
  char buffer[19];  // len("0x") + 16 digits + len("\0")
  snprintf(buffer, sizeof(buffer), "0x%016lx", n);
  return std::string(buffer);
}

}  // namespace hwstress
