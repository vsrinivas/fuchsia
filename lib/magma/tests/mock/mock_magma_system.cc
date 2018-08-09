// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_buffer.h"
#include "platform_semaphore.h"

#include <unordered_map>

std::unordered_map<uint32_t, magma::PlatformBuffer*> exported_buffers;
std::unordered_map<uint32_t, magma::PlatformSemaphore*> exported_semaphores;

class MockConnection : public magma_connection_t {
public:
    uint32_t next_context_id() { return next_context_id_++; }

private:
    uint32_t next_context_id_;
};

magma_connection_t* magma_create_connection(int32_t fd, uint32_t capabilities)
{
    return new MockConnection();
}

void magma_release_connection(magma_connection_t* connection)
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
        case MAGMA_QUERY_VENDOR_PARAM_0 + 1: // gtt size
            *value_out = 1ull << 32;
            return MAGMA_STATUS_OK;
    }
    return MAGMA_STATUS_INVALID_ARGS;
}

void magma_create_context(magma_connection_t* connection, uint32_t* context_id_out)
{
    *context_id_out = static_cast<MockConnection*>(connection)->next_context_id();
}

void magma_release_context(magma_connection_t* connection, uint32_t context_id) {}

magma_status_t magma_create_buffer(magma_connection_t* connection, uint64_t size,
                                   uint64_t* size_out, magma_buffer_t* buffer_out)
{
    auto buffer = magma::PlatformBuffer::Create(size, "magma-alloc");
    *buffer_out = reinterpret_cast<magma_buffer_t>(buffer.release());
    *size_out = size;
    return MAGMA_STATUS_OK;
}

void magma_release_buffer(magma_connection_t* connection, magma_buffer_t buffer)
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

magma_status_t magma_map_aligned(magma_connection_t* connection, magma_buffer_t buffer,
                                 uint64_t alignment, void** addr_out)
{
    if (!reinterpret_cast<magma::PlatformBuffer*>(buffer)->MapCpu(addr_out, alignment))
        return MAGMA_STATUS_MEMORY_ERROR;
    return MAGMA_STATUS_OK;
}

magma_status_t magma_unmap(magma_connection_t* connection, magma_buffer_t buffer)
{
    if (!reinterpret_cast<magma::PlatformBuffer*>(buffer)->UnmapCpu())
        return MAGMA_STATUS_MEMORY_ERROR;
    return MAGMA_STATUS_OK;
}

magma_status_t magma_create_command_buffer(magma_connection_t* connection, uint64_t size,
                                           magma_buffer_t* buffer_out)
{
    uint64_t size_out;
    return magma_create_buffer(connection, size, &size_out, buffer_out);
}

void magma_release_command_buffer(struct magma_connection_t* connection,
                                  magma_buffer_t command_buffer)
{
    magma_release_buffer(connection, command_buffer);
}

void magma_submit_command_buffer(struct magma_connection_t* connection, uint64_t command_buffer_id,
                                 uint32_t context_id)
{
    DLOG("magma_system submit command buffer - STUB");
}

magma_status_t magma_export(magma_connection_t* connection, magma_buffer_t buffer,
                            uint32_t* buffer_handle_out)
{
    uint32_t handle;
    reinterpret_cast<magma::PlatformBuffer*>(buffer)->duplicate_handle(&handle);
    exported_buffers[handle] = magma::PlatformBuffer::Import(handle).release();
    *buffer_handle_out = handle;
    return MAGMA_STATUS_OK;
}

magma_status_t magma_import(magma_connection_t* connection, uint32_t buffer_handle,
                            magma_buffer_t* buffer_out)
{
    *buffer_out = reinterpret_cast<magma_buffer_t>(exported_buffers[buffer_handle]);
    exported_buffers.erase(buffer_handle);
    return MAGMA_STATUS_OK;
}

magma_status_t magma_create_semaphore(magma_connection_t* connection,
                                      magma_semaphore_t* semaphore_out)
{
    *semaphore_out =
        reinterpret_cast<magma_semaphore_t>(magma::PlatformSemaphore::Create().release());
    return MAGMA_STATUS_OK;
}

void magma_release_semaphore(magma_connection_t* connection, magma_semaphore_t semaphore)
{
    delete reinterpret_cast<magma::PlatformSemaphore*>(semaphore);
}

uint64_t magma_get_semaphore_id(magma_semaphore_t semaphore)
{
    return reinterpret_cast<magma::PlatformSemaphore*>(semaphore)->id();
}

void magma_signal_semaphore(magma_semaphore_t semaphore) {}

void magma_reset_semaphore(magma_semaphore_t semaphore) {}

magma_status_t magma_wait_semaphores(const magma_semaphore_t* semaphore, uint32_t count,
                                     uint64_t timeout, magma_bool_t wait_all)
{
    return MAGMA_STATUS_OK;
}

magma_status_t magma_export_semaphore(magma_connection_t* connection, magma_semaphore_t semaphore,
                                      uint32_t* semaphore_handle_out)
{
    uint32_t handle;
    reinterpret_cast<magma::PlatformSemaphore*>(semaphore)->duplicate_handle(&handle);
    exported_semaphores[handle] = magma::PlatformSemaphore::Import(handle).release();
    *semaphore_handle_out = handle;
    return MAGMA_STATUS_OK;
}

magma_status_t magma_import_semaphore(magma_connection_t* connection, uint32_t semaphore_handle,
                                      magma_semaphore_t* semaphore_out)
{
    *semaphore_out = reinterpret_cast<magma_semaphore_t>(exported_semaphores[semaphore_handle]);
    exported_semaphores.erase(semaphore_handle);
    return MAGMA_STATUS_OK;
}

void magma_map_buffer_gpu(struct magma_connection_t* connection, magma_buffer_t buffer,
                          uint64_t gpu_va, uint64_t page_offset, uint64_t page_count,
                          uint64_t map_flags)
{
}

int32_t magma_get_notification_channel_fd(struct magma_connection_t* connection) { return 0; }

// Read a notification from the channel into |buffer|. Sets |*buffer_size_out| to 0 if there are no
// messages pending.
magma_status_t magma_read_notification_channel(struct magma_connection_t* connection, void* buffer,
                                               uint64_t buffer_size, uint64_t* buffer_size_out)
{
    return MAGMA_STATUS_UNIMPLEMENTED;
}
