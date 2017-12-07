// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_PCI_DEVICE_H
#define MSD_INTEL_PCI_DEVICE_H

#include "core/msd_intel_device_core.h"
#include "platform_pci_device.h"

class MsdIntelPciDevice : public magma::PlatformPciDevice {
public:
    // magma::PlatformPciDevice overrides
    void* GetDeviceHandle() override;
    bool ReadPciConfig16(uint64_t addr, uint16_t* value) override;
    std::unique_ptr<magma::PlatformMmio>
    CpuMapPciMmio(unsigned int pci_bar, magma::PlatformMmio::CachePolicy cache_policy) override;
    std::unique_ptr<magma::PlatformInterrupt> RegisterInterrupt() override;

    static std::unique_ptr<MsdIntelPciDevice> Create(void* device_handle);

    MsdIntelDeviceCore* device() { return device_.get(); }

private:
    MsdIntelPciDevice(std::unique_ptr<MsdIntelDeviceCore> device);

    std::unique_ptr<MsdIntelDeviceCore> device_;
};

#endif // MSD_INTEL_PCI_DEVICE_H
