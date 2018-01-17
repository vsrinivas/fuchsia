// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/msd_intel_device_core.h"
#include "msd_intel_pci_device.h"

class MsdIntelPciDeviceCore : public MsdIntelPciDevice {
public:
    MsdIntelPciDeviceCore(std::unique_ptr<MsdIntelDeviceCore> device) : device_(std::move(device))
    {
    }

    // magma::PlatformPciDevice overrides
    void* GetDeviceHandle() override { return device_->platform_device()->GetDeviceHandle(); }

    bool ReadPciConfig16(uint64_t addr, uint16_t* value) override
    {
        return device_->platform_device()->ReadPciConfig16(addr, value);
    }

    std::unique_ptr<magma::PlatformMmio>
    CpuMapPciMmio(unsigned int pci_bar, magma::PlatformMmio::CachePolicy cache_policy) override
    {
        return device_->platform_device()->CpuMapPciMmio(pci_bar, cache_policy);
    }

    bool RegisterInterruptCallback(InterruptManager::InterruptCallback callback, void* data,
                                   uint32_t interrupt_mask) override
    {
        return device_->RegisterCallback(callback, data, interrupt_mask);
    }

    void UnregisterInterruptCallback() override { return device_->UnregisterCallback(); }

    Gtt* GetGtt() override { return device_->gtt(); }

    void PresentBuffer(uint32_t buffer_handle, magma_system_image_descriptor* image_desc,
                       std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores,
                       std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores,
                       present_buffer_callback_t callback) override
    {
        return device_->PresentBuffer(buffer_handle, image_desc, std::move(wait_semaphores),
                                      std::move(signal_semaphores), callback);
    }

private:
    std::unique_ptr<MsdIntelDeviceCore> device_;
};

///////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<MsdIntelPciDevice> MsdIntelPciDevice::CreateCore(void* platform_device_handle)
{
    auto device = MsdIntelDeviceCore::Create(platform_device_handle);
    if (!device)
        return DRETP(nullptr, "couldn't create core device");

    return std::make_unique<MsdIntelPciDeviceCore>(std::move(device));
}
