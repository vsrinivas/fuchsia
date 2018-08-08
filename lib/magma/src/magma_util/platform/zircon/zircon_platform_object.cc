// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_object.h"

#include "magma_util/macros.h"
#include <zircon/syscalls.h>

namespace magma {

bool PlatformObject::IdFromHandle(uint32_t handle, uint64_t* id_out)
{
    zx_info_handle_basic_t info;
    zx_status_t status =
        zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    if (status != ZX_OK)
        return DRETF(false, "zx_object_get_info failed");

    *id_out = info.koid;
    return true;
}

} // namespace magma
