// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_HOST_SCHEDULER_PROFILE_H_
#define SRC_DEVICES_HOST_SCHEDULER_PROFILE_H_

#include <zircon/types.h>

namespace devmgr {

zx_status_t devhost_connect_scheduler_profile_provider();
zx_status_t devhost_get_scheduler_profile(uint32_t priority, const char* name,
                                          zx_handle_t* profile);

zx_status_t devhost_get_scheduler_deadline_profile(uint64_t capacity, uint64_t deadline,
                                                   uint64_t period, const char* name,
                                                   zx_handle_t* profile);
}  // namespace devmgr

#endif  // SRC_DEVICES_HOST_SCHEDULER_PROFILE_H_
