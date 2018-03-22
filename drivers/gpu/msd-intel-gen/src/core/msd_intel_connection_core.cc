// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/macros.h"
#include "msd.h"

void msd_connection_close(msd_connection_t* connection) {}

msd_context_t* msd_connection_create_context(msd_connection_t* abi_connection)
{
    return DRETP(nullptr, "not implemented for core");
}

magma_status_t msd_connection_wait_rendering(msd_connection_t* abi_connection, msd_buffer_t* buffer)
{
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "not implemented for core");
}

void msd_connection_set_notification_channel(msd_connection_t* connection,
                                             msd_channel_send_callback_t callback,
                                             msd_channel_t channel)
{
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

void msd_context_destroy(msd_context_t* ctx) {}

magma_status_t msd_context_execute_command_buffer(msd_context_t* ctx, msd_buffer_t* cmd_buf,
                                                  msd_buffer_t** exec_resources,
                                                  msd_semaphore_t** wait_semaphores,
                                                  msd_semaphore_t** signal_semaphores)
{
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "not implemented for core");
}

void msd_context_release_buffer(msd_context_t* context, msd_buffer_t* buffer) {}

magma_status_t msd_context_execute_immediate_commands(msd_context_t* ctx, uint64_t commands_size,
                                                      void* commands, uint64_t semaphore_count,
                                                      msd_semaphore_t** msd_semaphores)
{
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "not implemented for core");
}
