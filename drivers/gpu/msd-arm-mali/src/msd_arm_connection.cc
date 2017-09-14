// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_arm_connection.h"
#include "magma_util/dlog.h"
#include "platform_semaphore.h"

#include <vector>

void msd_connection_close(msd_connection_t* connection)
{
    delete MsdArmAbiConnection::cast(connection);
}

msd_context_t* msd_connection_create_context(msd_connection_t* abi_connection) { return nullptr; }

void msd_connection_present_buffer(msd_connection_t* abi_connection, msd_buffer_t* abi_buffer,
                                   magma_system_image_descriptor* image_desc,
                                   uint32_t wait_semaphore_count, uint32_t signal_semaphore_count,
                                   msd_semaphore_t** semaphores,
                                   msd_present_buffer_callback_t callback, void* callback_data)
{
}

magma_status_t msd_connection_wait_rendering(msd_connection_t* abi_connection, msd_buffer_t* buffer)
{
    DASSERT(false);
    return MAGMA_STATUS_INVALID_ARGS;
}

std::unique_ptr<MsdArmConnection> MsdArmConnection::Create(msd_client_id_t client_id)
{
    return std::make_unique<MsdArmConnection>(client_id);
}
