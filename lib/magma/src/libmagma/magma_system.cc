// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system.h"
#include "magenta/magenta_platform_ioctl.h"
#include "magma_util/macros.h"
#include "platform_connection.h"
#include "platform_semaphore.h"
#include <vector>

magma_system_connection* magma_system_open(int fd, uint32_t capabilities)
{
    magma_system_connection_request request;
    request.client_id = 0;
    request.capabilities = capabilities;

    uint32_t device_handle;
    int ioctl_ret = mxio_ioctl(fd, IOCTL_MAGMA_CONNECT, &request, sizeof(request), &device_handle,
                               sizeof(device_handle));
    if (ioctl_ret < 0)
        return DRETP(nullptr, "mxio_ioctl failed: %d", ioctl_ret);

    // Here we release ownership of the connection to the client
    return magma::PlatformIpcConnection::Create(device_handle).release();
}

void magma_system_close(magma_system_connection* connection)
{
    // TODO(MA-109): close the connection
    delete magma::PlatformIpcConnection::cast(connection);
}

magma_status_t magma_system_get_error(magma_system_connection* connection)
{
    return magma::PlatformIpcConnection::cast(connection)->GetError();
}

// Returns the device id.  0 is an invalid device id.
uint32_t magma_system_get_device_id(int fd)
{
    uint32_t device_id;
    int ioctl_ret =
        mxio_ioctl(fd, IOCTL_MAGMA_GET_DEVICE_ID, NULL, 0, &device_id, sizeof(device_id));
    if (ioctl_ret < 0) {
        DLOG("mxio_ioctl failed: %d", ioctl_ret);
        return 0;
    }
    return device_id;
}

void magma_system_create_context(magma_system_connection* connection, uint32_t* context_id_out)
{
    magma::PlatformIpcConnection::cast(connection)->CreateContext(context_id_out);
}

void magma_system_destroy_context(magma_system_connection* connection, uint32_t context_id)
{
    magma::PlatformIpcConnection::cast(connection)->DestroyContext(context_id);
}

magma_status_t magma_system_alloc(magma_system_connection* connection, uint64_t size,
                                  uint64_t* size_out, magma_buffer_t* buffer_out)
{
    auto platform_buffer = magma::PlatformBuffer::Create(size);
    if (!platform_buffer)
        return DRET(MAGMA_STATUS_MEMORY_ERROR);

    magma_status_t result =
        magma::PlatformIpcConnection::cast(connection)->ImportBuffer(platform_buffer.get());
    if (result != MAGMA_STATUS_OK)
        return DRET(result);

    *size_out = platform_buffer->size();
    *buffer_out =
        reinterpret_cast<magma_buffer_t>(platform_buffer.release()); // Ownership passed across abi

    return MAGMA_STATUS_OK;
}

void magma_system_free(magma_system_connection* connection, magma_buffer_t buffer)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);
    magma::PlatformIpcConnection::cast(connection)->ReleaseBuffer(platform_buffer->id());
    delete platform_buffer;
}

uint64_t magma_system_get_buffer_id(magma_buffer_t buffer)
{
    return reinterpret_cast<magma::PlatformBuffer*>(buffer)->id();
}

uint64_t magma_system_get_buffer_size(magma_buffer_t buffer)
{
    return reinterpret_cast<magma::PlatformBuffer*>(buffer)->size();
}

magma_status_t magma_system_import(magma_system_connection* connection, uint32_t buffer_handle,
                                   magma_buffer_t* buffer_out)
{
    auto platform_buffer = magma::PlatformBuffer::Import(buffer_handle);
    if (!platform_buffer)
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "PlatformBuffer::Import failed");

    magma_status_t result =
        magma::PlatformIpcConnection::cast(connection)->ImportBuffer(platform_buffer.get());
    if (result != MAGMA_STATUS_OK)
        return DRET_MSG(result, "ImportBuffer failed");

    *buffer_out = reinterpret_cast<magma_buffer_t>(platform_buffer.release());

    return MAGMA_STATUS_OK;
}

magma_status_t magma_system_export(magma_system_connection* connection, magma_buffer_t buffer,
                                   uint32_t* buffer_handle_out)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);

    if (!platform_buffer->duplicate_handle(buffer_handle_out))
        return DRET(MAGMA_STATUS_INVALID_ARGS);

    return MAGMA_STATUS_OK;
}

void magma_system_set_tiling_mode(magma_system_connection* connection, magma_buffer_t buffer,
                                  uint32_t tiling_mode)
{
    DLOG("magma_system_set_tiling_mode unimplemented");
}

magma_status_t magma_system_map(magma_system_connection* connection, magma_buffer_t buffer,
                                void** addr_out)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);

    if (!platform_buffer->MapCpu(addr_out))
        return DRET(MAGMA_STATUS_MEMORY_ERROR);

    return MAGMA_STATUS_OK;
}

magma_status_t magma_system_unmap(magma_system_connection* connection, magma_buffer_t buffer)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);

    if (!platform_buffer->UnmapCpu())
        return DRET(MAGMA_STATUS_MEMORY_ERROR);

    return MAGMA_STATUS_OK;
}

void magma_system_set_domain(magma_system_connection* connection, magma_buffer_t buffer,
                             uint32_t read_domains, uint32_t write_domain)
{
    DLOG("magma_system_set_tiling_mode unimplemented");
}

void magma_system_submit_command_buffer(magma_system_connection* connection,
                                        magma_buffer_t command_buffer, uint32_t context_id)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(command_buffer);

    magma::PlatformIpcConnection::cast(connection)
        ->ExecuteCommandBuffer(platform_buffer->id(), context_id);
}

void magma_system_wait_rendering(magma_system_connection* connection, magma_buffer_t buffer)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);

    magma::PlatformIpcConnection::cast(connection)->WaitRendering(platform_buffer->id());
}

void magma_system_display_page_flip(magma_system_connection* connection, magma_buffer_t buffer,
                                    uint32_t wait_semaphore_count,
                                    const magma_semaphore_t* wait_semaphores,
                                    uint32_t signal_semaphore_count,
                                    const magma_semaphore_t* signal_semaphores)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);

    std::vector<uint64_t> semaphore_ids(wait_semaphore_count + signal_semaphore_count);
    uint32_t index = 0;
    for (uint32_t i = 0; i < wait_semaphore_count; i++) {
        semaphore_ids[index++] = magma_system_get_semaphore_id(wait_semaphores[i]);
    }
    for (uint32_t i = 0; i < signal_semaphore_count; i++) {
        semaphore_ids[index++] = magma_system_get_semaphore_id(signal_semaphores[i]);
    }

    magma::PlatformIpcConnection::cast(connection)
        ->PageFlip(platform_buffer->id(), wait_semaphore_count, signal_semaphore_count,
                   semaphore_ids.data());
}

magma_status_t magma_system_create_semaphore(magma_system_connection* connection,
                                             magma_semaphore_t* semaphore_out)
{
    auto semaphore = magma::PlatformSemaphore::Create();
    if (!semaphore)
        return MAGMA_STATUS_MEMORY_ERROR;

    uint32_t handle;
    if (!semaphore->duplicate_handle(&handle))
        return DRET_MSG(MAGMA_STATUS_ACCESS_DENIED, "failed to duplicate handle");

    magma_status_t result = magma::PlatformIpcConnection::cast(connection)
                                ->ImportObject(handle, magma::PlatformObject::SEMAPHORE);
    if (result != MAGMA_STATUS_OK)
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to ImportObject");

    *semaphore_out = reinterpret_cast<magma_semaphore_t>(semaphore.release());
    return MAGMA_STATUS_OK;
}

void magma_system_destroy_semaphore(magma_system_connection* connection,
                                    magma_semaphore_t semaphore)
{
    auto platform_semaphore = reinterpret_cast<magma::PlatformSemaphore*>(semaphore);
    magma::PlatformIpcConnection::cast(connection)
        ->ReleaseObject(platform_semaphore->id(), magma::PlatformObject::SEMAPHORE);
    delete platform_semaphore;
}

uint64_t magma_system_get_semaphore_id(magma_semaphore_t semaphore)
{
    return reinterpret_cast<magma::PlatformSemaphore*>(semaphore)->id();
}

void magma_system_signal_semaphore(magma_semaphore_t semaphore)
{
    reinterpret_cast<magma::PlatformSemaphore*>(semaphore)->Signal();
}

magma_status_t magma_system_wait_semaphore(magma_semaphore_t semaphore, uint64_t timeout)
{
    if (!reinterpret_cast<magma::PlatformSemaphore*>(semaphore)->Wait(timeout))
        return MAGMA_STATUS_TIMED_OUT;

    return MAGMA_STATUS_OK;
}
