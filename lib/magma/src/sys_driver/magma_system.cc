// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system.h"
#include "magenta/magenta_platform_ioctl.h"
#include "magma_util/macros.h"
#include "platform_connection.h"
#include <errno.h>

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

int32_t magma_system_get_error(magma_system_connection* connection)
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

int32_t magma_system_alloc(magma_system_connection* connection, uint64_t size, uint64_t* size_out,
                           uint32_t* buffer_id_out)
{
    auto buf = magma::PlatformBuffer::Create(size);
    if (!buf)
        return DRET(-ENOMEM);

    *size_out = buf->size();
    // TODO(MA-104) Use 64 bit buffer ids everywhere
    DASSERT(buf->id() <= UINT32_MAX);
    *buffer_id_out = buf->id();

    if (!magma::PlatformIpcConnection::cast(connection)->ImportBuffer(std::move(buf)))
        return DRET(-EINVAL);

    return 0;
}

void magma_system_free(magma_system_connection* connection, uint32_t buffer_id)
{
    magma::PlatformIpcConnection::cast(connection)->ReleaseBuffer(buffer_id);
}

int32_t magma_system_import(magma_system_connection* connection, uint32_t buffer_handle,
                            uint32_t* buffer_id_out)
{
    uint64_t id;
    if (!magma::PlatformBuffer::IdFromHandle(buffer_handle, &id))
        return DRET(-EINVAL);

    auto buf = magma::PlatformIpcConnection::cast(connection)->LookupBuffer(id);
    if (!buf) {
        // if we dont have the buffer already create it
        auto new_buf = magma::PlatformBuffer::Import(buffer_handle);
        if (!new_buf)
            return DRET(-EINVAL);
        if (!magma::PlatformIpcConnection::cast(connection)->ImportBuffer(std::move(new_buf)))
            return DRET(-EINVAL);
        buf = magma::PlatformIpcConnection::cast(connection)->LookupBuffer(id);
        DASSERT(buf);
    }

    // TODO(MA-104) Use 64 bit buffer ids everywhere
    DASSERT(buf->id() <= UINT32_MAX);
    *buffer_id_out = buf->id();

    return 0;
}

int32_t magma_system_export(magma_system_connection* connection, uint32_t buffer_id,
                            uint32_t* buffer_handle_out)
{

    auto buf = magma::PlatformIpcConnection::cast(connection)->LookupBuffer(buffer_id);
    if (!buf)
        return DRET(-EINVAL);

    if (!buf->duplicate_handle(buffer_handle_out))
        return DRET(-EINVAL);

    return 0;
}

void magma_system_set_tiling_mode(magma_system_connection* connection, uint32_t buffer_id,
                                  uint32_t tiling_mode)
{
    DLOG("magma_system_set_tiling_mode unimplemented");
}

int32_t magma_system_map(magma_system_connection* connection, uint32_t buffer_id, void** addr_out)
{
    auto buf = magma::PlatformIpcConnection::cast(connection)->LookupBuffer(buffer_id);
    if (!buf)
        return DRET(-EINVAL);

    if (!buf->MapCpu(addr_out))
        return DRET(-EINVAL);

    return 0;
}

int32_t magma_system_unmap(magma_system_connection* connection, uint32_t buffer_id, void* addr)
{
    auto buf = magma::PlatformIpcConnection::cast(connection)->LookupBuffer(buffer_id);
    if (!buf)
        return DRET(-EINVAL);

    if (!buf->UnmapCpu())
        return DRET(-EINVAL);

    return 0;
}

void magma_system_set_domain(magma_system_connection* connection, uint32_t buffer_id,
                             uint32_t read_domains, uint32_t write_domain)
{
    DLOG("magma_system_set_tiling_mode unimplemented");
}

void magma_system_submit_command_buffer(magma_system_connection* connection,
                                        uint32_t command_buffer_id, uint32_t context_id)
{
    magma::PlatformIpcConnection::cast(connection)
        ->ExecuteCommandBuffer(command_buffer_id, context_id);
}

void magma_system_wait_rendering(magma_system_connection* connection, uint32_t buffer_id)
{

    magma::PlatformIpcConnection::cast(connection)->WaitRendering(buffer_id);
}

void magma_system_display_page_flip(magma_system_connection* connection, uint32_t buffer_id,
                                    magma_system_pageflip_callback_t callback, void* data)
{

    magma::PlatformIpcConnection::cast(connection)->PageFlip(buffer_id, callback, data);
}
