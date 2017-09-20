// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_driver.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "msd_intel_device.h"

MsdIntelDriver::MsdIntelDriver() { magic_ = kMagic; }

std::unique_ptr<MsdIntelDriver> MsdIntelDriver::Create()
{
    return std::unique_ptr<MsdIntelDriver>(new MsdIntelDriver());
}

void MsdIntelDriver::Destroy(MsdIntelDriver* drv) { delete drv; }

//////////////////////////////////////////////////////////////////////////////

msd_driver_t* msd_driver_create(void) { return MsdIntelDriver::Create().release(); }

void msd_driver_configure(struct msd_driver_t* drv, uint32_t flags)
{
    MsdIntelDriver::cast(drv)->configure(flags);
}

void msd_driver_destroy(msd_driver_t* drv) { MsdIntelDriver::Destroy(MsdIntelDriver::cast(drv)); }

msd_device_t* msd_driver_create_device(msd_driver_t* drv, void* device_handle)
{
    bool start_device_thread = (MsdIntelDriver::cast(drv)->configure_flags() &
                                MSD_DRIVER_CONFIG_TEST_NO_DEVICE_THREAD) == 0;

    std::unique_ptr<MsdIntelDevice> device =
        MsdIntelDevice::Create(device_handle, start_device_thread);
    if (!device)
        return DRETP(nullptr, "failed to create device");

    // Transfer ownership across the ABI
    return device.release();
}
