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

void msd_connection_release_buffer(msd_connection_t* connection, msd_buffer_t* buffer)
{
    MsdIntelAbiConnection::cast(connection)
        ->ptr()
        ->ReleaseBuffer(MsdIntelAbiBuffer::cast(buffer)->ptr()->platform_buffer());
}

void MsdIntelConnection::ReleaseBuffer(magma::PlatformBuffer* buffer)
{
    std::vector<std::shared_ptr<GpuMapping>> mappings;
    per_process_gtt()->ReleaseBuffer(buffer, &mappings);

    bool killed = false;
    for (const auto& mapping : mappings) {
        uint32_t use_count = mapping.use_count();
        if (use_count == 1) {
            // Bus mappings are held in the connection and passed through the command stream to
            // ensure the memory isn't released until the tlbs are invalidated, which happens
            // implicitly on every pipeline flush.
            mappings_to_release_.push_back(mapping->Release());
        } else {
            // It's an error to release a buffer while it has inflight mappings, as that
            // can fault the gpu.
            DLOG("mapping use_count %d", use_count);
            if (!killed) {
                SendContextKilled();
                killed = true;
            }
        }
    }
}

bool MsdIntelConnection::SubmitPendingReleaseMappings(std::shared_ptr<MsdIntelContext> context)
{
    if (!mappings_to_release_.empty()) {
        magma::Status status = SubmitBatch(
            std::make_unique<MappingReleaseBatch>(context, std::move(mappings_to_release_)));
        mappings_to_release_.clear();
        if (!status.ok())
            return DRETF(false, "Failed to submit mapping release batch: %d", status.get());
    }
    return true;
}

std::unique_ptr<MsdIntelConnection> MsdIntelConnection::Create(Owner* owner,
                                                               msd_client_id_t client_id)
{
    return std::unique_ptr<MsdIntelConnection>(
        new MsdIntelConnection(owner, PerProcessGtt::Create(owner), client_id));
}
