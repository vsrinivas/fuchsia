// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_buffer.h"

#include <unordered_map>

class MockConnection : public magma_system_connection {
public:
    uint32_t next_context_id() { return next_context_id_++; }

private:
    uint32_t next_context_id_;
};

magma_system_connection* magma_system_open(int32_t fd, uint32_t capabilities)
{
    return new MockConnection();
}

void magma_system_close(magma_system_connection* connection)
{
    delete static_cast<MockConnection*>(connection);
}

magma_status_t magma_system_get_error(magma_system_connection* connection) { return 0; }

// Returns the device id.  0 is an invalid device id.
uint32_t magma_system_get_device_id(int32_t fd) { return 0x1916; }

void magma_system_create_context(magma_system_connection* connection, uint32_t* context_id_out)
{
    *context_id_out = static_cast<MockConnection*>(connection)->next_context_id();
}

void magma_system_destroy_context(magma_system_connection* connection, uint32_t context_id) {}

magma_status_t magma_system_alloc(magma_system_connection* connection, uint64_t size,
                                  uint64_t* size_out, magma_buffer_t* buffer_out)
{
    auto buffer = magma::PlatformBuffer::Create(size);
    *buffer_out = reinterpret_cast<magma_buffer_t>(buffer.release());
    *size_out = size;
    return MAGMA_STATUS_OK;
}

void magma_system_free(magma_system_connection* connection, magma_buffer_t buffer)
{
    delete reinterpret_cast<magma::PlatformBuffer*>(buffer);
}

uint64_t magma_system_get_buffer_id(magma_buffer_t buffer)
{
    return reinterpret_cast<magma::PlatformBuffer*>(buffer)->id();
}

uint64_t magma_system_get_buffer_size(magma_buffer_t buffer)
{
    return reinterpret_cast<magma::PlatformBuffer*>(buffer)->size();
}

magma_status_t magma_system_map(magma_system_connection* connection, magma_buffer_t buffer,
                                void** addr_out)
{
    if (!reinterpret_cast<magma::PlatformBuffer*>(buffer)->MapCpu(addr_out))
        return MAGMA_STATUS_MEMORY_ERROR;
    return MAGMA_STATUS_OK;
}

magma_status_t magma_system_unmap(magma_system_connection* connection, magma_buffer_t buffer)
{
    if (!reinterpret_cast<magma::PlatformBuffer*>(buffer)->UnmapCpu())
        return MAGMA_STATUS_MEMORY_ERROR;
    return MAGMA_STATUS_OK;
}

void magma_system_set_tiling_mode(magma_system_connection* connection, uint64_t buffer_id,
                                  uint32_t tiling_mode)
{
}

void magma_system_set_domain(magma_system_connection* connection, magma_buffer_t buffer,
                             uint32_t read_domains, uint32_t write_domain)
{
}

void magma_system_submit_command_buffer(struct magma_system_connection* connection,
                                        uint64_t command_buffer_id, uint32_t context_id)
{
    DLOG("magma_system submit command buffer - STUB");
}

void magma_system_wait_rendering(magma_system_connection* connection, uintptr_t buffer) {}

magma_status_t magma_system_export(magma_system_connection* connection, magma_buffer_t buffer,
                                   uint32_t* buffer_handle_out)
{
    return MAGMA_STATUS_OK;
}

int32_t magma_system_import(magma_system_connection* connection, uint32_t buffer_handle,
                            magma_buffer_t* buffer_out)
{

    return 0;
}

void magma_system_display_page_flip(magma_system_connection* connection, uint64_t buffer_id,
                                    uint32_t wait_semaphore_count,
                                    const magma_semaphore_t* wait_semaphores,
                                    uint32_t signal_semaphore_count,
                                    const magma_semaphore_t* signal_semaphores)
{
}

magma_status_t magma_system_create_semaphore(magma_system_connection* connection,
                                             magma_semaphore_t* semaphore_out)
{
    return MAGMA_STATUS_OK;
}

void magma_system_destroy_semaphore(magma_system_connection* connection,
                                    magma_semaphore_t semaphore)
{
}

uint64_t magma_system_get_semaphore_id(magma_semaphore_t semaphore) { return 0; }

void magma_system_signal_semaphore(magma_semaphore_t semaphore) {}

void magma_system_reset_semaphore(magma_semaphore_t semaphore) {}

magma_status_t magma_system_wait_semaphore(magma_semaphore_t semaphore, uint64_t timeout)
{
    return MAGMA_STATUS_OK;
}
