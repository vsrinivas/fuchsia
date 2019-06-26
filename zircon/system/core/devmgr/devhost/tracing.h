// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_DEVMGR_DEVHOST_TRACING_H_
#define ZIRCON_SYSTEM_CORE_DEVMGR_DEVHOST_TRACING_H_

#include <zircon/types.h>

namespace devmgr {

// Register the devhost as a "trace provider" with the trace manager.
// There is no corresponding "unregister" function: we remain registered
// until either us or the manager terminate.
zx_status_t devhost_start_trace_provider();

}  // namespace devmgr

#endif  // ZIRCON_SYSTEM_CORE_DEVMGR_DEVHOST_TRACING_H_
