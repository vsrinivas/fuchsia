// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_driver.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "msd_intel_device.h"

MsdIntelDriver::MsdIntelDriver() { magic_ = kMagic; }

MsdIntelDriver* MsdIntelDriver::Create()
{
    auto drv = new MsdIntelDriver();
    if (!drv) {
        DLOG("Failed to allocate MsdIntelDriver");
        return nullptr;
    }
    return drv;
}

void MsdIntelDriver::Destroy(MsdIntelDriver* drv) { delete drv; }

std::unique_ptr<MsdIntelDevice> MsdIntelDriver::CreateDevice(void* device_handle)
{
    std::unique_ptr<MsdIntelDevice> dev(new MsdIntelDevice());

    if (!dev->Init(device_handle))
        return DRETP(nullptr, "Failed to initialize MsdIntelDevice");

    return dev;
}

//////////////////////////////////////////////////////////////////////////////

msd_driver* msd_driver_create(void) { return MsdIntelDriver::Create(); }

void msd_driver_destroy(msd_driver* drv) { MsdIntelDriver::Destroy(MsdIntelDriver::cast(drv)); }

msd_device* msd_driver_create_device(msd_driver* drv, void* device)
{
    // Transfer ownership across the ABI
    return MsdIntelDriver::cast(drv)->CreateDevice(device).release();
}

void msd_device_destroy(msd_device* dev) { delete MsdIntelDevice::cast(dev); }
