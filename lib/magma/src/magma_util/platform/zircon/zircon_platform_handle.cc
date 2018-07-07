// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_handle.h"
#include <zx/handle.h>

namespace magma {

bool ZirconPlatformHandle::GetCount(uint32_t* count_out)
{
    zx_info_handle_count_t info;
    zx_status_t status =
        zx_object_get_info(get(), ZX_INFO_HANDLE_COUNT, &info, sizeof(info), nullptr, nullptr);
    if (status != ZX_OK)
        return DRETF(false, "zx_object_get_info failed");

    *count_out = info.handle_count;
    return true;
}

std::unique_ptr<PlatformHandle> PlatformHandle::Create(uint32_t handle)
{
    return std::make_unique<ZirconPlatformHandle>(zx::handle(handle));
}

} // namespace magma
