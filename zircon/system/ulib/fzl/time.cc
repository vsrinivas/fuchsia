// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <lib/fzl/time.h>
#include <lib/zx/time.h>

namespace fzl {

zx::ticks NsToTicks(zx::duration ns) {
    return zx::ticks(ns_to_ticks(ns.get()));
}

zx::duration TicksToNs(zx::ticks ticks) {
    return zx::duration(ticks_to_ns(ticks.get()));
}

} // namespace fzl

zx_ticks_t ns_to_ticks(zx_duration_t ns) {
    return static_cast<zx_ticks_t>(static_cast<__uint128_t>(ns) * zx_ticks_per_second() /
                                   ZX_SEC(1));
}

zx_duration_t ticks_to_ns(zx_ticks_t ticks) {
    return static_cast<zx_duration_t>(static_cast<__uint128_t>(ticks) * ZX_SEC(1) /
                                      zx_ticks_per_second());
}
