// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_DRIVER_H_
#define _MAGMA_DRIVER_H_

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "msd.h"

#include "magma_system_device.h"

using msd_driver_unique_ptr_t = std::unique_ptr<msd_driver, std::function<void(msd_driver*)>>;

class MagmaDriver {
public:
    MagmaDriver(msd_driver_unique_ptr_t arch) : arch_(std::move(arch)) {}

    MagmaSystemDevice* CreateDevice(void* device)
    {
        DASSERT(!g_device);

        msd_device* arch_dev = msd_driver_create_device(arch_.get(), device);
        if (!arch_dev) {
            return DRETP(nullptr, "msd_create_device failed");;
        }

        g_device =
            new MagmaSystemDevice(msd_device_unique_ptr_t(arch_dev, &msd_driver_destroy_device));

        return g_device;
    }

    static MagmaDriver* Create()
    {
        msd_driver* arch = msd_driver_create();
        if (!arch) {
            DLOG("msd_create returned null");
            return nullptr;
        }

        auto driver = new MagmaDriver(msd_driver_unique_ptr_t(arch, msd_driver_destroy));

        return driver;
    }

    static MagmaSystemDevice* GetDevice() { return g_device; }

private:
    msd_driver_unique_ptr_t arch_;
    static MagmaSystemDevice* g_device;
};

#endif // MAGMA_DRIVER_H
