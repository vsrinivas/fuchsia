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

void msd_connection_set_notification_callback(struct msd_connection_t* connection,
                                              msd_connection_notification_callback_t callback,
                                              void* token)
{
    MsdIntelAbiConnection::cast(connection)->ptr()->SetNotificationCallback(callback, token);
}

magma_status_t msd_connection_map_buffer_gpu(msd_connection_t* connection, msd_buffer_t* buffer,
                                             uint64_t gpu_va, uint64_t page_offset,
                                             uint64_t page_count, uint64_t flags)
{
    return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t msd_connection_unmap_buffer_gpu(msd_connection_t* connection, msd_buffer_t* buffer,
                                               uint64_t gpu_va)
{
    return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t msd_connection_commit_buffer(msd_connection_t* connection, msd_buffer_t* buffer,
                                            uint64_t page_offset, uint64_t page_count)
{
    return MAGMA_STATUS_UNIMPLEMENTED;
}

void msd_connection_release_buffer(msd_connection_t* connection, msd_buffer_t* buffer) {}

std::unique_ptr<MsdIntelConnection> MsdIntelConnection::Create(Owner* owner,
                                                               msd_client_id_t client_id)
{
    std::unique_ptr<GpuMappingCache> cache;
#if MSD_INTEL_ENABLE_MAPPING_CACHE
    cache = GpuMappingCache::Create();
#endif
    return std::unique_ptr<MsdIntelConnection>(
        new MsdIntelConnection(owner, PerProcessGtt::Create(owner, std::move(cache)), client_id));
}
