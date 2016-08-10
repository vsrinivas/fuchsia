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

#include "magma_system.h"
#include "magma_driver.h"
#include "magma_system_connection.h"
#include "magma_util/macros.h"

#ifdef __linux__
#include <unistd.h>
static msd_client_id get_client_id() { return static_cast<msd_client_id>(getpid()); }
#else
static msd_client_id get_client_id() { return static_cast<msd_client_id>(1); }
#endif // __linux__

MagmaSystemDevice* MagmaDriver::g_device;

MagmaSystemConnection* magma_system_open(uint32_t device_handle)
{
    if (device_handle != 0xdeadbeef)
        return DRETP(nullptr, "Unexpected device_handle");


    MagmaSystemDevice* dev = MagmaDriver::GetDevice();
    DASSERT(dev);

    msd_client_id client_id = get_client_id();

    auto connection = dev->Open(client_id);
    if (!connection)
        return DRETP(nullptr, "failed to open device");

    // Here we release ownership of the connection to the client
    return connection.release();
}

void magma_system_close(MagmaSystemConnection* connection) { delete connection; }

// Returns the device id.  0 is an invalid device id.
uint32_t magma_system_get_device_id(MagmaSystemConnection* connection)
{
    return connection->GetDeviceId();
}

bool magma_system_create_context(MagmaSystemConnection* connection, uint32_t* context_id_out)
{
    return connection->CreateContext(context_id_out);
}

bool magma_system_destroy_context(MagmaSystemConnection* connection, uint32_t context_id)
{
    return connection->DestroyContext(context_id);
}

bool magma_system_alloc(MagmaSystemConnection* connection, uint64_t size, uint64_t* size_out,
                        uint32_t* handle_out)
{
    auto buf = connection->AllocateBuffer(size);
    if (!buf)
        return false;

    *size_out = buf->size();
    *handle_out = buf->handle();
    return true;
}

bool magma_system_free(MagmaSystemConnection* connection, uint32_t handle)
{
    return connection->FreeBuffer(handle);
}

bool magma_system_set_tiling_mode(MagmaSystemConnection* connection, uint32_t handle,
                                  uint32_t tiling_mode)
{
    DLOG("TODO: magma_system_set_tiling_mode");
    return false;
}

bool magma_system_map(MagmaSystemConnection* connection, uint32_t handle, void** paddr)
{
    auto buf = connection->LookupBuffer(handle);
    if (!buf)
        return false;

    return buf->platform_buffer()->MapCpu(paddr);
}

bool magma_system_unmap(MagmaSystemConnection* connection, uint32_t handle, void* addr)
{
    auto buf = connection->LookupBuffer(handle);
    if (!buf)
        return false;

    return buf->platform_buffer()->UnmapCpu();
}

bool magma_system_set_domain(MagmaSystemConnection* connection, uint32_t handle,
                             uint32_t read_domains, uint32_t write_domain)
{
    DLOG("TODO: magma_system_set_domain");
    return false;
}

bool magma_system_execute_buffer(MagmaSystemConnection* connection,
                                 struct MagmaExecBuffer* execbuffer)
{
    DLOG("TODO: magma_system_execute_buffer");
    return false;
}

void magma_system_wait_rendering(MagmaSystemConnection* connection, uint32_t handle)
{
    DLOG("TODO: magma_system_wait_rendering");
}
