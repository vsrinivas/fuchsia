// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "msd_intel_device_core.h"
#include "msd_intel_driver.h"

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
    // Core device has been allocated for us.
    return reinterpret_cast<MsdIntelDeviceCore*>(device_handle);
}
