// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

#ifdef __cplusplus

#include <lib/zx/time.h>

namespace fzl {

zx::ticks NsToTicks(zx::duration ns);
zx::duration TicksToNs(zx::ticks ticks);

} // namespace fzl

#endif // _cplusplus

__BEGIN_CDECLS

zx_ticks_t ns_to_ticks(zx_duration_t ns);
zx_duration_t ticks_to_ns(zx_ticks_t ticks);

__END_CDECLS
