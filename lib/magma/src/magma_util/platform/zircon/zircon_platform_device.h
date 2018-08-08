// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_PLATFORM_DEVICE_H
#define ZIRCON_PLATFORM_DEVICE_H

#include "platform_device.h"

#include <ddk/device.h>
#include <ddk/protocol/platform-device.h>

namespace magma {

class ZirconPlatformDevice : public PlatformDevice {
public:
    ZirconPlatformDevice(zx_device_t* zx_device, platform_device_protocol_t pdev)
        : zx_device_(zx_device), pdev_(pdev)
    {
    }

    void* GetDeviceHandle() override { return zx_device(); }

    std::unique_ptr<PlatformHandle> GetBusTransactionInitiator() override;

    std::unique_ptr<PlatformMmio> CpuMapMmio(unsigned int index,
                                             PlatformMmio::CachePolicy cache_policy) override;

    std::unique_ptr<PlatformInterrupt> RegisterInterrupt(unsigned int index) override;

private:
    zx_device_t* zx_device() const { return zx_device_; }

    zx_device_t* zx_device_;
    platform_device_protocol_t pdev_;
};

} // namespace magma

#endif // ZIRCON_PLATFORM_DEVICE_H
