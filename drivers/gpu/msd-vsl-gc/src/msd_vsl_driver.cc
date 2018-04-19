// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_vsl_driver.h"
#include "magma_util/macros.h"
#include "msd.h"

msd_driver_t* msd_driver_create(void) { return new MsdVslDriver(); }

void msd_driver_configure(struct msd_driver_t* drv, uint32_t flags) {}

void msd_driver_destroy(msd_driver_t* drv) { delete MsdVslDriver::cast(drv); }

msd_device_t* msd_driver_create_device(msd_driver_t* drv, void* device_handle)
{
    return DRETP(nullptr, "not implemented");
}
