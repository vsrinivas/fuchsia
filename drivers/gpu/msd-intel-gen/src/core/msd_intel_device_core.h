// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_DEVICE_CORE_H
#define MSD_INTEL_DEVICE_CORE_H

#include "gtt.h"
#include "interrupt_manager.h"
#include "platform_pci_device.h"

// Implements core device functionality;
// May be replaced with a shim to a different driver.
class MsdIntelDeviceCore final : public Gtt::Owner, InterruptManager::Owner {
public:
    magma::PlatformPciDevice* platform_device() override { return platform_device_.get(); }

    bool RegisterCallback(InterruptManager::InterruptCallback callback, void* data,
                          uint32_t interrupt_mask)
    {
        if (forwarding_mask_)
            return DRETF(false, "callback already registered");

        DASSERT(callback);
        forwarding_data_ = data;
        forwarding_callback_ = callback;
        forwarding_mask_ = interrupt_mask;

        return true;
    }

    void DeleteInterruptManager() { interrupt_manager_.reset(); }

    Gtt* gtt() { return gtt_.get(); }

    static std::unique_ptr<MsdIntelDeviceCore> Create(void* device_handle);

private:
    MsdIntelDeviceCore() {}

    bool Init(void* device_handle);

    RegisterIo* register_io_for_interrupt() override { return register_io_.get(); }

    static void InterruptCallback(void* data, uint32_t master_interrupt_control);

    std::unique_ptr<Gtt> gtt_;
    std::unique_ptr<magma::PlatformPciDevice> platform_device_;
    std::unique_ptr<RegisterIo> register_io_;
    std::unique_ptr<InterruptManager> interrupt_manager_;

    InterruptManager::InterruptCallback forwarding_callback_;
    void* forwarding_data_;
    std::atomic<uint32_t> forwarding_mask_{0};
};

#endif // MSD_INTEL_DEVICE_CORE_H
