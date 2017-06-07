// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAGENTA_PLATFORM_DEVICE_H
#define MAGENTA_PLATFORM_DEVICE_H

#include "platform_device.h"

#include <ddk/device.h>

namespace magma {

class MagentaPlatformDevice : public PlatformDevice {
public:
    MagentaPlatformDevice(mx_device_t* mx_device, pci_protocol_t* pci, pci_config_t* cfg,
                          size_t cfg_size, mx_handle_t cfg_handle)
        : mx_device_(mx_device), pci_(pci), cfg_(cfg), cfg_size_(cfg_size), cfg_handle_(cfg_handle)
    {
    }

    ~MagentaPlatformDevice() override;

    void* GetDeviceHandle() override { return mx_device(); }

    bool ReadPciConfig16(uint64_t addr, uint16_t* value) override;

    std::unique_ptr<PlatformMmio> CpuMapPciMmio(unsigned int pci_bar,
                                                PlatformMmio::CachePolicy cache_policy) override;

    std::unique_ptr<PlatformInterrupt> RegisterInterrupt() override;

private:
    mx_device_t* mx_device() const { return mx_device_; }
    pci_protocol_t* pci() const { return pci_; }
    pci_config_t* pci_config() const { return cfg_; }

    mx_device_t* mx_device_;
    pci_protocol_t* pci_;
    pci_config_t* cfg_;
    size_t cfg_size_;
    mx_handle_t cfg_handle_;
};

} // namespace

#endif // MAGENTA_PLATFORM_DEVICE_H
