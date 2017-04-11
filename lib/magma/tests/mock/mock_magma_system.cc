// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_buffer.h"

#include <unordered_map>

class MockConnection : public magma_connection_t {
public:
    uint32_t next_context_id() { return next_context_id_++; }

private:
    uint32_t next_context_id_;
};

magma_connection_t* magma_open(int32_t fd, uint32_t capabilities) { return new MockConnection(); }

void magma_close(magma_connection_t* connection)
{
    delete static_cast<MockConnection*>(connection);
}

magma_status_t magma_get_error(magma_connection_t* connection) { return 0; }

magma_status_t magma_query(int32_t fd, uint64_t id, uint64_t* value_out)
{
    switch (id) {
        case MAGMA_QUERY_DEVICE_ID:
            *value_out = 0x1916;
            return MAGMA_STATUS_OK;
        case MAGMA_QUERY_VENDOR_PARAM_0:
            *value_out = (23l << 32) | 8;
            return MAGMA_STATUS_OK;
    }
    return MAGMA_STATUS_INVALID_ARGS;
}

void magma_create_context(magma_connection_t* connection, uint32_t* context_id_out)
{
    *context_id_out = static_cast<MockConnection*>(connection)->next_context_id();
}

void magma_destroy_context(magma_connection_t* connection, uint32_t context_id) {}

magma_status_t magma_alloc(magma_connection_t* connection, uint64_t size, uint64_t* size_out,
                           magma_buffer_t* buffer_out)
{
    auto buffer = magma::PlatformBuffer::Create(size);
    *buffer_out = reinterpret_cast<magma_buffer_t>(buffer.release());
    *size_out = size;
    return MAGMA_STATUS_OK;
}

void magma_free(magma_connection_t* connection, magma_buffer_t buffer)
{
    delete reinterpret_cast<magma::PlatformBuffer*>(buffer);
}

uint64_t magma_get_buffer_id(magma_buffer_t buffer)
{
    return reinterpret_cast<magma::PlatformBuffer*>(buffer)->id();
}

uint64_t magma_get_buffer_size(magma_buffer_t buffer)
{
    return reinterpret_cast<magma::PlatformBuffer*>(buffer)->size();
}

magma_status_t magma_map(magma_connection_t* connection, magma_buffer_t buffer, void** addr_out)
{
    if (!reinterpret_cast<magma::PlatformBuffer*>(buffer)->MapCpu(addr_out))
        return MAGMA_STATUS_MEMORY_ERROR;
    return MAGMA_STATUS_OK;
}

magma_status_t magma_unmap(magma_connection_t* connection, magma_buffer_t buffer)
{
    if (!reinterpret_cast<magma::PlatformBuffer*>(buffer)->UnmapCpu())
        return MAGMA_STATUS_MEMORY_ERROR;
    return MAGMA_STATUS_OK;
}

void magma_submit_command_buffer(struct magma_connection_t* connection, uint64_t command_buffer_id,
                                 uint32_t context_id)
{
    DLOG("magma_system submit command buffer - STUB");
}

void magma_wait_rendering(magma_connection_t* connection, uintptr_t buffer) {}

magma_status_t magma_export(magma_connection_t* connection, magma_buffer_t buffer,
                            uint32_t* buffer_handle_out)
{
    return MAGMA_STATUS_OK;
}

int32_t magma_import(magma_connection_t* connection, uint32_t buffer_handle,
                     magma_buffer_t* buffer_out)
{

    return 0;
}

void magma_display_page_flip(magma_connection_t* connection, uint64_t buffer_id,
                             uint32_t wait_semaphore_count,
                             const magma_semaphore_t* wait_semaphores,
                             uint32_t signal_semaphore_count,
                             const magma_semaphore_t* signal_semaphores)
{
}

magma_status_t magma_create_semaphore(magma_connection_t* connection,
                                      magma_semaphore_t* semaphore_out)
{
    return MAGMA_STATUS_OK;
}

void magma_destroy_semaphore(magma_connection_t* connection, magma_semaphore_t semaphore) {}

uint64_t magma_get_semaphore_id(magma_semaphore_t semaphore) { return 0; }

void magma_signal_semaphore(magma_semaphore_t semaphore) {}

void magma_reset_semaphore(magma_semaphore_t semaphore) {}

magma_status_t magma_wait_semaphore(magma_semaphore_t semaphore, uint64_t timeout)
{
    return MAGMA_STATUS_OK;
}
