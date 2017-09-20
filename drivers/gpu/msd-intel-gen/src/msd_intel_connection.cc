// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_connection.h"
#include "magma_util/dlog.h"
#include "msd_intel_semaphore.h"
#include "ppgtt.h"

void msd_connection_close(msd_connection_t* connection)
{
    delete MsdIntelAbiConnection::cast(connection);
}

msd_context_t* msd_connection_create_context(msd_connection_t* abi_connection)
{
    auto connection = MsdIntelAbiConnection::cast(abi_connection)->ptr();

    // Backing store creation deferred until context is used.
    return new MsdIntelAbiContext(
        std::make_unique<ClientContext>(connection, connection->per_process_gtt()));
}

void msd_connection_present_buffer(msd_connection_t* abi_connection, msd_buffer_t* abi_buffer,
                                   magma_system_image_descriptor* image_desc,
                                   uint32_t wait_semaphore_count, uint32_t signal_semaphore_count,
                                   msd_semaphore_t** semaphores,
                                   msd_present_buffer_callback_t callback, void* callback_data)
{
    auto connection = MsdIntelAbiConnection::cast(abi_connection)->ptr();

    std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores(wait_semaphore_count);
    uint32_t index = 0;
    for (uint32_t i = 0; i < wait_semaphore_count; i++) {
        wait_semaphores[i] = MsdIntelAbiSemaphore::cast(semaphores[index++])->ptr();
    }
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores(
        signal_semaphore_count);
    for (uint32_t i = 0; i < signal_semaphore_count; i++) {
        signal_semaphores[i] = MsdIntelAbiSemaphore::cast(semaphores[index++])->ptr();
    }

    connection->PresentBuffer(
        MsdIntelAbiBuffer::cast(abi_buffer)->ptr(), image_desc, std::move(wait_semaphores),
        std::move(signal_semaphores),
        [callback, callback_data](magma_status_t status, uint64_t vblank_time_ns) {
            if (callback)
                callback(status, vblank_time_ns, callback_data);
        });
}

magma_status_t msd_connection_wait_rendering(msd_connection_t* abi_connection, msd_buffer_t* buffer)
{
    auto connection = MsdIntelAbiConnection::cast(abi_connection)->ptr();

    if (connection->context_killed())
        return DRET(MAGMA_STATUS_CONTEXT_KILLED);

    MsdIntelAbiBuffer::cast(buffer)->ptr()->WaitRendering();

    if (connection->context_killed())
        return DRET(MAGMA_STATUS_CONTEXT_KILLED);

    return MAGMA_STATUS_OK;
}

std::unique_ptr<MsdIntelConnection>
MsdIntelConnection::Create(Owner* owner, std::shared_ptr<magma::PlatformBuffer> scratch_buffer,
                           msd_client_id_t client_id)
{
    std::unique_ptr<GpuMappingCache> cache;
#if MSD_INTEL_ENABLE_MAPPING_CACHE
    cache = GpuMappingCache::Create();
#endif
    return std::unique_ptr<MsdIntelConnection>(new MsdIntelConnection(
        owner, PerProcessGtt::Create(std::move(scratch_buffer), std::move(cache)), client_id));
}
