// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

void msd_device_destroy(msd_device* dev)
{
    // TODO(MA-28) should be
    // MsdMockDriver::cast(drv)->DestroyDevice(MsdMockDevice::cast(dev));
    delete MsdMockDevice::cast(dev);
}

void msd_device_page_flip(msd_device* dev, msd_buffer* buf,
                          magma_system_pageflip_callback_t callback, void* data)
{
    callback(0, data);
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

int32_t msd_connection_wait_rendering(struct msd_connection* connection, struct msd_buffer* buf)
{
    return 0;
}

void msd_context_destroy(msd_context* ctx) { delete MsdMockContext::cast(ctx); }

msd_buffer* msd_buffer_import(uint32_t handle)
{
    if (!g_bufmgr)
        g_bufmgr.reset(new MsdMockBufferManager());
    return g_bufmgr->CreateBuffer(handle);
}

void msd_buffer_destroy(msd_buffer* buf)
{
    if (!g_bufmgr)
        g_bufmgr.reset(new MsdMockBufferManager());

    if (buf)
        return g_bufmgr->DestroyBuffer(MsdMockBuffer::cast(buf));
}

int32_t msd_context_execute_command_buffer(msd_context* ctx, msd_buffer* cmd_buf,
                                           msd_buffer** exec_resources)
{
    return MsdMockContext::cast(ctx)->ExecuteCommandBuffer(cmd_buf, exec_resources);
}

void MsdMockBufferManager::SetTestBufferManager(std::unique_ptr<MsdMockBufferManager> bufmgr)
{
    g_bufmgr = std::move(bufmgr);
}

MsdMockBufferManager* MsdMockBufferManager::ScopedMockBufferManager::get()
{
    return g_bufmgr.get();
}

MsdMockContext::~MsdMockContext() { connection_->DestroyContext(this); }
