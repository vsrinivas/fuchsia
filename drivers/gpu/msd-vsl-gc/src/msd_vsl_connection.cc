// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_vsl_connection.h"
#include "msd_vsl_context.h"

void msd_connection_close(msd_connection_t* connection)
{
    delete MsdVslAbiConnection::cast(connection);
}

msd_context_t* msd_connection_create_context(msd_connection_t* abi_connection)
{
    return new MsdVslAbiContext(std::make_shared<MsdVslContext>(
        MsdVslAbiConnection::cast(abi_connection)->ptr()->address_space()));
}

magma_status_t msd_connection_map_buffer_gpu(msd_connection_t* abi_connection,
                                             msd_buffer_t* abi_buffer, uint64_t gpu_va,
                                             uint64_t page_offset, uint64_t page_count,
                                             uint64_t flags)
{
    return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t msd_connection_unmap_buffer_gpu(msd_connection_t* abi_connection,
                                               msd_buffer_t* buffer, uint64_t gpu_va)
{
    return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t msd_connection_commit_buffer(msd_connection_t* abi_connection,
                                            msd_buffer_t* abi_buffer, uint64_t page_offset,
                                            uint64_t page_count)
{
    return MAGMA_STATUS_UNIMPLEMENTED;
}

void msd_connection_set_notification_callback(struct msd_connection_t* connection,
                                              msd_connection_notification_callback_t callback,
                                              void* token)
{
}

void msd_connection_release_buffer(msd_connection_t* abi_connection, msd_buffer_t* abi_buffer) {}
