// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/protocol/platform-device.h>
#include <zircon/process.h>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_mmio.h"

namespace magma {

class ZirconPlatformMmio : public PlatformMmio {
public:
    ZirconPlatformMmio(void* addr, uint64_t size, zx_handle_t handle);

    ~ZirconPlatformMmio();

private:
    zx_handle_t handle_;
};

} // namespace magma
