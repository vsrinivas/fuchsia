// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_handle.h"
#include <zx/handle.h>

namespace magma {

std::unique_ptr<PlatformHandle> PlatformHandle::Create(uint32_t handle)
{
    return std::make_unique<ZirconPlatformHandle>(zx::handle(handle));
}

} // namespace magma
