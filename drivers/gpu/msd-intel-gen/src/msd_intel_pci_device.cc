// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_pci_device.h"

MsdIntelPciDevice::MsdIntelPciDevice(std::unique_ptr<MsdIntelDeviceCore> device)
    : device_(std::move(device))
{
}

void* MsdIntelPciDevice::GetDeviceHandle() { return device_->platform_device()->GetDeviceHandle(); }

bool MsdIntelPciDevice::ReadPciConfig16(uint64_t addr, uint16_t* value)
{
    return device_->platform_device()->ReadPciConfig16(addr, value);
}

std::unique_ptr<magma::PlatformMmio>
MsdIntelPciDevice::CpuMapPciMmio(unsigned int pci_bar,
                                 magma::PlatformMmio::CachePolicy cache_policy)
{
    return device_->platform_device()->CpuMapPciMmio(pci_bar, cache_policy);
}

std::unique_ptr<magma::PlatformInterrupt> MsdIntelPciDevice::RegisterInterrupt()
{
    DASSERT(false);
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<MsdIntelPciDevice> MsdIntelPciDevice::Create(void* device_handle)
{
    auto device = MsdIntelDeviceCore::Create(device_handle);
    if (!device)
        return DRETP(nullptr, "couldn't create core device");

    return std::unique_ptr<MsdIntelPciDevice>(new MsdIntelPciDevice(std::move(device)));
}
