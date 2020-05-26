// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <lib/zx/clock.h>
#include <lib/zx/time.h>

namespace hwstress {

zx::duration SecsToDuration(double secs) {
  return zx::nsec(static_cast<int64_t>(secs * 1'000'000'000.0));
}

double DurationToSecs(zx::duration d) { return d.to_nsecs() / 1'000'000'000.0; }

}  // namespace hwstress
