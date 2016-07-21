// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _MAGMA_DRIVER_H_
#define _MAGMA_DRIVER_H_

#include "magma_sys_driver.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"

#include "magma_system_device.h"

class MagmaDriver {
public:
    MagmaDriver(msd_driver* arch) : arch_(arch) {}

    MagmaSystemDevice* CreateDevice(void* device)
    {
        DASSERT(!g_device);

        msd_device* arch_dev = msd_driver_create_device(arch_, device);
        if (!arch_dev) {
            DLOG("msd_create_device failed");
            return nullptr;
        }

        g_device = new MagmaSystemDevice(arch_dev);
        if (!g_device) {
            DLOG("failed to allocate MagmaSystemDevice");
            // TODO: msd_destroy_device
            return nullptr;
        }

        return g_device;
    }

    static MagmaDriver* Create()
    {
        msd_driver* arch = msd_driver_create();
        if (!arch) {
            DLOG("msd_create returned null");
            return nullptr;
        }

        auto driver = new MagmaDriver(arch);
        if (!driver) {
            DLOG("fail to allocate MagmaDriver");
            return nullptr;
        }

        return driver;
    }

    static MagmaSystemDevice* GetDevice() { return g_device; }

private:
    msd_driver* arch_;
    static MagmaSystemDevice* g_device;
};

#endif // MAGMA_DRIVER_H
