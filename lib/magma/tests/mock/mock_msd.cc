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

#include "mock_msd.h"
#include "msd.h"

std::unique_ptr<MsdMockBufferManager> g_bufmgr;

struct msd_driver* msd_driver_create(void) { return new MsdMockDriver(); }

void msd_driver_destroy(msd_driver* drv) { delete MsdMockDriver::cast(drv); }

msd_device* msd_driver_create_device(msd_driver* drv, void* device)
{
    // If youre passing something meaningful in here youre #doingitwrong
    DASSERT(!device);

    return MsdMockDriver::cast(drv)->CreateDevice();
}

void msd_driver_destroy_device(msd_device* dev)
{
    // TODO(MA-28) should be
    // MsdMockDriver::cast(drv)->DestroyDevice(MsdMockDevice::cast(dev));
    delete MsdMockDevice::cast(dev);
}

msd_connection* msd_device_open(msd_device* dev, msd_client_id client_id)
{
    return MsdMockDevice::cast(dev)->Open(client_id);
}

void msd_connection_close(msd_connection* connection)
{
    delete MsdMockConnection::cast(connection);
}

uint32_t msd_device_get_id(msd_device* dev) { return MsdMockDevice::cast(dev)->GetDeviceId(); }

msd_context* msd_connection_create_context(msd_connection* dev)
{
    return MsdMockConnection::cast(dev)->CreateContext();
}

void msd_connection_destroy_context(msd_connection* dev, msd_context* ctx)
{
    MsdMockConnection::cast(dev)->DestroyContext(MsdMockContext::cast(ctx));
}

msd_buffer* msd_buffer_import(msd_platform_buffer* platform_buf)
{
    if (!g_bufmgr)
        g_bufmgr.reset(new MsdMockBufferManager());
    return g_bufmgr->CreateBuffer(platform_buf);
}

void msd_buffer_destroy(msd_buffer* buf)
{
    if (!g_bufmgr)
        g_bufmgr.reset(new MsdMockBufferManager());

    if (buf)
        return g_bufmgr->DestroyBuffer(MsdMockBuffer::cast(buf));
}

void MsdMockBufferManager::SetTestBufferManager(std::unique_ptr<MsdMockBufferManager> bufmgr)
{
    g_bufmgr = std::move(bufmgr);
}

MsdMockBufferManager* MsdMockBufferManager::ScopedMockBufferManager::get()
{
    return g_bufmgr.get();
}
