// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock_msd.h"
#include "msd.h"
#include "platform_semaphore.h"
#include <vector>

std::unique_ptr<MsdMockBufferManager> g_bufmgr;

struct msd_driver_t* msd_driver_create(void) { return new MsdMockDriver(); }

void msd_driver_configure(struct msd_driver_t* drv, uint32_t flags) {}

void msd_driver_destroy(msd_driver_t* drv) { delete MsdMockDriver::cast(drv); }

msd_device_t* msd_driver_create_device(msd_driver_t* drv, void* device)
{
    // If youre passing something meaningful in here youre #doingitwrong
    DASSERT(!device);

    return MsdMockDriver::cast(drv)->CreateDevice();
}

void msd_device_destroy(msd_device_t* dev)
{
    // TODO(MA-28) should be
    // MsdMockDriver::cast(drv)->DestroyDevice(MsdMockDevice::cast(dev));
    delete MsdMockDevice::cast(dev);
}

void msd_device_page_flip(msd_device_t* dev, msd_buffer_t* buf,
                          magma_system_image_descriptor* image_desc, uint32_t wait_semaphore_count,
                          uint32_t signal_semaphore_count, msd_semaphore_t** semaphores)
{
    static std::vector<magma::PlatformSemaphore*> last_semaphores;

    for (uint32_t i = 0; i < last_semaphores.size(); i++) {
        last_semaphores[i]->Signal();
    }

    last_semaphores.clear();

    for (uint32_t i = wait_semaphore_count; i < wait_semaphore_count + signal_semaphore_count;
         i++) {
        last_semaphores.push_back(reinterpret_cast<magma::PlatformSemaphore*>(semaphores[i]));
    }
}

msd_connection_t* msd_device_open(msd_device_t* dev, msd_client_id client_id)
{
    return MsdMockDevice::cast(dev)->Open(client_id);
}

void msd_connection_close(msd_connection_t* connection)
{
    delete MsdMockConnection::cast(connection);
}

uint32_t msd_device_get_id(msd_device_t* dev) { return MsdMockDevice::cast(dev)->GetDeviceId(); }

msd_context_t* msd_connection_create_context(msd_connection_t* dev)
{
    return MsdMockConnection::cast(dev)->CreateContext();
}

magma_status_t msd_connection_wait_rendering(struct msd_connection_t* connection,
                                             struct msd_buffer_t* buf)
{
    return MAGMA_STATUS_OK;
}

void msd_context_destroy(msd_context_t* ctx) { delete MsdMockContext::cast(ctx); }

msd_buffer_t* msd_buffer_import(uint32_t handle)
{
    if (!g_bufmgr)
        g_bufmgr.reset(new MsdMockBufferManager());
    return g_bufmgr->CreateBuffer(handle);
}

void msd_buffer_destroy(msd_buffer_t* buf)
{
    if (!g_bufmgr)
        g_bufmgr.reset(new MsdMockBufferManager());

    if (buf)
        return g_bufmgr->DestroyBuffer(MsdMockBuffer::cast(buf));
}

magma_status_t msd_context_execute_command_buffer(msd_context_t* ctx, msd_buffer_t* cmd_buf,
                                                  msd_buffer_t** exec_resources,
                                                  msd_semaphore_t** wait_semaphores,
                                                  msd_semaphore_t** signal_semaphores)
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

magma_status_t msd_semaphore_import(uint32_t handle, msd_semaphore_t** semaphore_out)
{
    *semaphore_out =
        reinterpret_cast<msd_semaphore_t*>(magma::PlatformSemaphore::Import(handle).release());
    DASSERT(*semaphore_out);
    return MAGMA_STATUS_OK;
}

void msd_semaphore_release(msd_semaphore_t* semaphore)
{
    delete reinterpret_cast<magma::PlatformSemaphore*>(semaphore);
}
