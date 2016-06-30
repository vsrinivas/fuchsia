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

#include <magma_sys_driver.h>
#include <magma_util/dlog.h>
#include <magma_util/macros.h>

struct MagmaSystemDevice {
public:
    MagmaSystemDevice(MsdDevice* arch_dev) : arch_dev_(arch_dev) {}

    MsdDevice* arch() { return arch_dev_; }

    void set_client_id(ClientId client_id) { client_id_ = client_id; }
    ClientId client_id() { return client_id_; }

private:
    ClientId client_id_{};
    MsdDevice* arch_dev_;
};

class MagmaDriver {
public:
    MagmaDriver(MagmaSysDriver* arch) : arch_(arch) {}

    MagmaSystemDevice* CreateDevice(void* device)
    {
        DASSERT(!g_device);

        MsdDevice* arch_dev = msd_create_device(arch_, device);
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
        MagmaSysDriver* arch = msd_create();
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
    MagmaSysDriver* arch_;
    static MagmaSystemDevice* g_device;
};

#endif // MAGMA_DRIVER_H
