// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_PLATFORM_PCI_DEVICE_H
#define ZIRCON_PLATFORM_PCI_DEVICE_H

#include "platform_pci_device.h"

#include <ddk/device.h>

namespace magma {

class ZirconPlatformPciDevice : public PlatformPciDevice {
public:
    ZirconPlatformPciDevice(zx_device_t* zx_device, pci_protocol_t& pci, pci_config_t* cfg,
                            size_t cfg_size, zx_handle_t cfg_handle)
        : zx_device_(zx_device), pci_(pci), cfg_(cfg), cfg_size_(cfg_size), cfg_handle_(cfg_handle)
    {
    }

    ~ZirconPlatformPciDevice() override;

    void* GetDeviceHandle() override { return zx_device(); }

    bool ReadPciConfig16(uint64_t addr, uint16_t* value) override;

    std::unique_ptr<PlatformMmio> CpuMapPciMmio(unsigned int pci_bar,
                                                PlatformMmio::CachePolicy cache_policy) override;

    std::unique_ptr<PlatformInterrupt> RegisterInterrupt() override;

private:
    zx_device_t* zx_device() const { return zx_device_; }
    const pci_protocol_t& pci() const { return pci_; }
    pci_config_t* pci_config() const { return cfg_; }

    zx_device_t* zx_device_;
    pci_protocol_t pci_;
    pci_config_t* cfg_;
    size_t cfg_size_;
    zx_handle_t cfg_handle_;
};

} // namespace

#endif // ZIRCON_PLATFORM_PCI_DEVICE_H
