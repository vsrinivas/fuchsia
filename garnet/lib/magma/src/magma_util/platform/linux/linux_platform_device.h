// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_PLATFORM_DEVICE_H
#define ZIRCON_PLATFORM_DEVICE_H

#include "platform_device.h"

#include "linux_platform_mmio.h"
#include <magma_util/macros.h>

namespace magma {

class LinuxPlatformDevice : public PlatformDevice {
public:
    LinuxPlatformDevice(int file_descriptor) : fd_(file_descriptor) {}

    void* GetDeviceHandle() override { return reinterpret_cast<void*>(static_cast<intptr_t>(fd_)); }

    std::unique_ptr<PlatformHandle> GetSchedulerProfile(Priority priority,
                                                        const char* name) const override
    {
        return DRETP(nullptr, "GetSchedulerProfile not implemented");
    }

    std::unique_ptr<PlatformHandle> GetBusTransactionInitiator() const override { return nullptr; }

    Status LoadFirmware(const char* filename, std::unique_ptr<PlatformBuffer>* firmware_out,
                        uint64_t* size_out) const override
    {
        return DRET_MSG(MAGMA_STATUS_UNIMPLEMENTED, "LoadFirmware not implemented");
    }

    std::unique_ptr<PlatformMmio> CpuMapMmio(unsigned int index,
                                             PlatformMmio::CachePolicy cache_policy) override;

    std::unique_ptr<PlatformInterrupt> RegisterInterrupt(unsigned int index) override
    {
        return DRETP(nullptr, "RegisterInterrupt not implemented");
    }

private:
    enum class MagmaGetParamKey {
        kRegisterSize = 10,
    };

    bool MagmaGetParam(MagmaGetParamKey key, uint64_t* value_out);

    int fd_;
};

} // namespace magma

#endif // ZIRCON_PLATFORM_DEVICE_H
