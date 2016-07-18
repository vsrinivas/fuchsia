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

#include "intel_gen.h"
#include <magma_util/dlog.h>

class GenMagenta : public MagmaSysDriver {
public:
    MsdDevice* CreateDevice(void* device) override;
    void DestroyDevice(MsdDevice*) override;
};

MsdDevice* GenMagenta::CreateDevice(void* device)
{
    auto mx_dev = reinterpret_cast<mx_device_t*>(device);

    auto gen_dev = new MsdDevice(mx_dev);
    if (!gen_dev) {
        DLOG("Failed to allocate MagentaDevice");
        return nullptr;
    }

    return gen_dev;
}

void GenMagenta::DestroyDevice(MsdDevice* gen_dev) { delete gen_dev; }

MagmaSysDriver* MagmaSysDriver::New() { return new GenMagenta(); }

void MagmaSysDriver::Delete(MagmaSysDriver* gen) { delete gen; }

//////////////////////////////////////////////////////////////////////////////

MagmaSysDriver* msd_create(void) { return MagmaSysDriver::New(); }

void msd_destroy(MagmaSysDriver* drv) { MagmaSysDriver::Delete(drv); }

MsdDevice* msd_create_device(MagmaSysDriver* drv, void* device)
{
    auto msd_dev = drv->CreateDevice(device);
    return msd_dev;
}

void msd_destroy_device(MagmaSysDriver* drv, MsdDevice* msd_dev)
{
    DLOG("TODO msd_destroy_device");
}

bool msd_open(MsdDevice* msd_dev, ClientId client_id)
{
    DLOG("TODO msd_open");
    return false;
}

void msd_close(MsdDevice* msd_dev, ClientId client_id) { DLOG("TODO msd_close"); }

uint32_t msd_get_device_id(MsdDevice* msd_dev)
{
    DLOG("TODO msd_get_device_id");
    return 0;
}

bool msd_alloc(MsdDevice* msd_dev, uint64_t size, uint64_t* size_out, uint32_t* handle_out)
{
    DLOG("TODO msd_alloc");
    return false;
}

bool msd_free(MsdDevice* msd_dev, uint32_t handle)
{
    DLOG("TODO msd_free");
    return false;
}

bool msd_set_tiling_mode(MsdDevice* msd_dev, uint32_t handle, uint32_t tiling_mode)
{
    DLOG("TODO msd_set_tiling_mode");
    return false;
}

bool msd_map(MsdDevice* msd_dev, uint32_t handle, void** paddr)
{
    DLOG("TODO: msd_map");
    return false;
}

bool msd_unmap(MsdDevice* msd_dev, uint32_t handle, void* addr)
{
    DLOG("TODO: msd_unmap");
    return false;
}

bool msd_set_domain(MsdDevice* msd_dev, uint32_t handle, uint32_t read_domains,
                    uint32_t write_domain)
{
    DLOG("msd_set_domain");
    return false;
}

bool msd_subdata(MsdDevice* msd_dev, uint32_t handle, unsigned long offset, unsigned long size,
                 const void* data)
{
    DLOG("msd_subdata");
    return false;
}

bool msd_execute_buffer(MsdDevice* msd_dev, ClientId client_id, struct MagmaExecBuffer* execbuffer)
{
    DLOG("msd_execute_buffer");
    return false;
}

bool msd_create_context(MsdDevice* msd_dev, ClientId client_id, int* context_id)
{
    DLOG("TODO: msd_create_context");
    return false;
}

void msd_wait_rendering(MsdDevice* msd_dev, uint32_t handle) { DLOG("TODO: msd_wait_rendering"); }
