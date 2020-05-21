// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/clone_mono.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

namespace media::audio::clock {

void CloneMonotonicInto(zx::clock* clock_out, bool adjustable) {
  FX_DCHECK(clock_out) << "Out parameter cannot be null";
  auto status =
      zx::clock::create(ZX_CLOCK_OPT_AUTO_START | ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS,
                        nullptr, clock_out);
  FX_DCHECK(status == ZX_OK) << "Reference clock could not be created";

  if (!adjustable) {
    constexpr auto rights = ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ;
    status = clock_out->replace(rights, clock_out);
    FX_DCHECK(status == ZX_OK) << "Rights could not be replaced for reference clock";
  }
}

zx::clock AdjustableCloneOfMonotonic() {
  zx::clock clone;
  CloneMonotonicInto(&clone, true);

  return clone;
}

zx::clock CloneOfMonotonic() {
  zx::clock clone;
  CloneMonotonicInto(&clone, false);

  return clone;
}

}  // namespace media::audio::clock
