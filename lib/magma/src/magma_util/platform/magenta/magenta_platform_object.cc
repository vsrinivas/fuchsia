// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_object.h"

#include "magma_util/macros.h"
#include <magenta/syscalls.h>

namespace magma {

bool PlatformObject::IdFromHandle(uint32_t handle, uint64_t* id_out)
{
    mx_info_handle_basic_t info;
    mx_status_t status =
        mx_object_get_info(handle, MX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    if (status != MX_OK)
        return DRETF(false, "mx_object_get_info failed");

    *id_out = info.koid;
    return true;
}

} // namespace
