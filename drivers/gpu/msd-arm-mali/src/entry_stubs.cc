// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/magma/include/msd_abi/msd.h"

msd_connection_t* msd_device_open(msd_device_t* dev, msd_client_id_t client_id) { return nullptr; }

uint32_t msd_device_get_id(msd_device_t* dev) { return 0; }

void msd_connection_present_buffer(msd_connection_t* connection, msd_buffer_t* buf,
                                   struct magma_system_image_descriptor* image_desc,
                                   uint32_t wait_semaphore_count, uint32_t signal_semaphore_count,
                                   msd_semaphore_t** semaphores,
                                   msd_present_buffer_callback_t callback, void* callback_data)
{
}

void msd_connection_close(msd_connection_t* connection) {}

msd_context_t* msd_connection_create_context(msd_connection_t* connection) { return nullptr; }

magma_status_t msd_connection_wait_rendering(msd_connection_t* connection, msd_buffer_t* buf)
{
    return MAGMA_STATUS_OK;
}

void msd_context_destroy(msd_context_t* ctx) {}

magma_status_t msd_context_execute_command_buffer(msd_context_t* ctx, msd_buffer_t* cmd_buf,
                                                  msd_buffer_t** exec_resources,
                                                  msd_semaphore_t** wait_semaphores,
                                                  msd_semaphore_t** signal_semaphores)
{
    return MAGMA_STATUS_OK;
}

void msd_context_release_buffer(msd_context_t* context, msd_buffer_t* buffer) {}

msd_buffer_t* msd_buffer_import(uint32_t handle) { return nullptr; }

void msd_buffer_destroy(msd_buffer_t* buf) {}

magma_status_t msd_semaphore_import(uint32_t handle, msd_semaphore_t** semaphore_out)
{
    return MAGMA_STATUS_OK;
}

void msd_semaphore_release(msd_semaphore_t* semaphore) {}
