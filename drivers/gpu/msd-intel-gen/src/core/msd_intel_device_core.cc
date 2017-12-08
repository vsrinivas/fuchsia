// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_device_core.h"

std::unique_ptr<MsdIntelDeviceCore> MsdIntelDeviceCore::Create(void* device_handle)
{
    auto device = std::unique_ptr<MsdIntelDeviceCore>(new MsdIntelDeviceCore);
    if (!device->Init(device_handle))
        return DRETP(nullptr, "couldn't init device");

    return device;
}

bool MsdIntelDeviceCore::Init(void* device_handle)
{
    DASSERT(!platform_device_);
    DLOG("Init device_handle %p", device_handle);

    platform_device_ = magma::PlatformPciDevice::Create(device_handle);
    if (!platform_device_)
        return DRETF(false, "failed to create platform device");

    std::unique_ptr<magma::PlatformMmio> mmio(
        platform_device_->CpuMapPciMmio(0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE));
    if (!mmio)
        return DRETF(false, "failed to map pci bar 0");

    register_io_ = std::unique_ptr<RegisterIo>(new RegisterIo(std::move(mmio)));

    gtt_ = Gtt::CreateCore(this);

    interrupt_manager_ = InterruptManager::CreateCore(this);
    if (!interrupt_manager_)
        return DRETF(false, "failed to create interrupt manager");

    return true;
}
