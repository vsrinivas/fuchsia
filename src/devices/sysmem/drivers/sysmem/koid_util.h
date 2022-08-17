// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_KOID_UTIL_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_KOID_UTIL_H_

#include <lib/fpromise/result.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include <vector>

namespace sysmem_driver {

zx_status_t get_handle_koids(const zx::object_base& this_end, zx_koid_t* this_end_koid,
                             zx_koid_t* that_end_koid, zx_obj_type_t type);

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_KOID_UTIL_H_
