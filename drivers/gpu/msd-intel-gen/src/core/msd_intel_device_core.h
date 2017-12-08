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
        DASSERT(interrupt_manager_);
        return interrupt_manager_->RegisterCallback(callback, data, interrupt_mask);
    }

    void DeleteInterruptManager() { interrupt_manager_.reset(); }

    Gtt* gtt() { return gtt_.get(); }

    static std::unique_ptr<MsdIntelDeviceCore> Create(void* device_handle);

private:
    MsdIntelDeviceCore() {}

    bool Init(void* device_handle);

    RegisterIo* register_io_for_interrupt() override { return register_io_.get(); }

    std::unique_ptr<Gtt> gtt_;
    std::unique_ptr<magma::PlatformPciDevice> platform_device_;
    std::unique_ptr<RegisterIo> register_io_;
    std::unique_ptr<InterruptManager> interrupt_manager_;
};

#endif // MSD_INTEL_DEVICE_CORE_H
