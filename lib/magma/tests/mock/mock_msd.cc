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

magma_status_t msd_device_display_get_size(struct msd_device_t* dev,
                                           struct magma_display_size* size_out)
{
    return MAGMA_STATUS_INTERNAL_ERROR;
}

msd_connection_t* msd_device_open(msd_device_t* dev, msd_client_id_t client_id)
{
    return MsdMockDevice::cast(dev)->Open(client_id);
}

void msd_connection_close(msd_connection_t* connection)
{
    delete MsdMockConnection::cast(connection);
}

uint32_t msd_device_get_id(msd_device_t* dev) { return MsdMockDevice::cast(dev)->GetDeviceId(); }

magma_status_t msd_device_query(msd_device_t* device, uint64_t id, uint64_t* value_out)
{
    return MAGMA_STATUS_INVALID_ARGS;
}

msd_context_t* msd_connection_create_context(msd_connection_t* dev)
{
    return MsdMockConnection::cast(dev)->CreateContext();
}

void msd_connection_present_buffer(msd_connection_t* abi_connection, msd_buffer_t* abi_buffer,
                                   magma_system_image_descriptor* image_desc,
                                   uint32_t wait_semaphore_count, uint32_t signal_semaphore_count,
                                   msd_semaphore_t** semaphores,
                                   msd_present_buffer_callback_t callback, void* callback_data)
{
    static std::vector<magma::PlatformSemaphore*> last_semaphores;

    for (uint32_t i = 0; i < last_semaphores.size(); i++) {
        last_semaphores[i]->Signal();
    }

    last_semaphores.clear();

    if (callback)
        callback(MAGMA_STATUS_OK, 0, callback_data);

    for (uint32_t i = wait_semaphore_count; i < wait_semaphore_count + signal_semaphore_count;
         i++) {
        last_semaphores.push_back(reinterpret_cast<magma::PlatformSemaphore*>(semaphores[i]));
    }
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

void msd_context_release_buffer(msd_context_t* context, msd_buffer_t* buffer) {}

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

void msd_connection_map_buffer_gpu(struct msd_connection_t* connection, struct msd_buffer_t* buffer,
                                   uint64_t gpu_va, uint64_t flags)
{
}

void msd_connection_unmap_buffer_gpu(struct msd_connection_t* connection,
                                     struct msd_buffer_t* buffer, uint64_t gpu_va)
{
}

void msd_connection_commit_buffer(struct msd_connection_t* connection, struct msd_buffer_t* buffer,
                                  uint64_t page_offset, uint64_t page_count)
{
}
