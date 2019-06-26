// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_DEVMGR_DEVHOST_SCHEDULER_PROFILE_H_
#define ZIRCON_SYSTEM_CORE_DEVMGR_DEVHOST_SCHEDULER_PROFILE_H_

#include <zircon/types.h>

namespace devmgr {

zx_status_t devhost_connect_scheduler_profile_provider();
zx_status_t devhost_get_scheduler_profile(uint32_t priority, const char* name,
                                          zx_handle_t* profile);

}  // namespace devmgr

#endif  // ZIRCON_SYSTEM_CORE_DEVMGR_DEVHOST_SCHEDULER_PROFILE_H_
