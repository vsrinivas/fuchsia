// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_DEVICE_H
#define PLATFORM_DEVICE_H

#include "magma_util/dlog.h"
#include "platform_interrupt.h"
#include "platform_mmio.h"
#include <memory>

namespace magma {

class PlatformDevice {
public:
    virtual ~PlatformDevice() { DLOG("PlatformDevice dtor"); }

    virtual void* GetDeviceHandle() = 0;

    // Map an MMIO listed at |index| in the MDI for this device.
    virtual std::unique_ptr<PlatformMmio> CpuMapMmio(unsigned int index,
                                                     PlatformMmio::CachePolicy cache_policy)
    {
        DLOG("CpuMapMmio unimplemented");
        return nullptr;
    }

    // Register an interrupt listed at |index| in the MDI for this device.
    virtual std::unique_ptr<PlatformInterrupt> RegisterInterrupt(unsigned int index)
    {
        DLOG("RegisterInterrupt unimplemented");
        return nullptr;
    }

    static std::unique_ptr<PlatformDevice> Create(void* device_handle);
};

} // namespace magma

#endif // PLATFORM_DEVICE_H
