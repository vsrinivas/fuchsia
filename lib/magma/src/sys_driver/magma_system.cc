// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system.h"
#include "magenta/device/ioctl-wrapper.h"
#include "magma_driver.h"
#include "magma_system_connection.h"
#include "magma_util/macros.h"
#include <errno.h>

#ifdef __linux__
#include <unistd.h>
static msd_client_id get_client_id() { return static_cast<msd_client_id>(getpid()); }
#else
static msd_client_id get_client_id() { return static_cast<msd_client_id>(1); }
#endif // __linux__

MagmaSystemDevice* MagmaDriver::g_device;

magma_system_connection* magma_system_open(int fd)
{
    uint32_t device_handle;
    int ioctl_ret = mxio_ioctl(fd, 1, NULL, 0, &device_handle, sizeof(device_handle));
    if (ioctl_ret < 0)
        return DRETP(nullptr, "mxio_ioctl failed: %d", ioctl_ret);

    if (device_handle != 0xdeadbeef)
        return DRETP(nullptr, "Unexpected device_handle");

    MagmaSystemDevice* dev = MagmaDriver::GetDevice();
    DASSERT(dev);

    msd_client_id client_id = get_client_id();

    auto connection = dev->Open(client_id);
    if (!connection)
        return DRETP(nullptr, "failed to open device");

    // Here we release ownership of the connection to the client
    return connection.release();
}

void magma_system_close(magma_system_connection* connection) { delete connection; }

// Returns the device id.  0 is an invalid device id.
uint32_t magma_system_get_device_id(int fd)
{
    uint32_t device_id;
    int ioctl_ret = mxio_ioctl(fd, 0, NULL, 0, &device_id, sizeof(device_id));
    if (ioctl_ret < 0) {
        DLOG("mxio_ioctl failed: %d", ioctl_ret);
        return 0;
    }
    return device_id;
}

bool magma_system_create_context(magma_system_connection* connection, uint32_t* context_id_out)
{
    return MagmaSystemConnection::cast(connection)->CreateContext(context_id_out);
}

bool magma_system_destroy_context(magma_system_connection* connection, uint32_t context_id)
{
    return MagmaSystemConnection::cast(connection)->DestroyContext(context_id);
}

bool magma_system_alloc(magma_system_connection* connection, uint64_t size, uint64_t* size_out,
                        uint32_t* handle_out)
{
    auto buf = MagmaSystemConnection::cast(connection)->AllocateBuffer(size);
    if (!buf)
        return false;

    *size_out = buf->size();
    *handle_out = buf->handle();
    return true;
}

bool magma_system_free(magma_system_connection* connection, uint32_t handle)
{
    return MagmaSystemConnection::cast(connection)->FreeBuffer(handle);
}

bool magma_system_export(magma_system_connection* connection, uint32_t handle, uint32_t* token_out)
{
    return MagmaSystemConnection::cast(connection)->ExportBuffer(handle, token_out);
}

bool magma_system_set_tiling_mode(magma_system_connection* connection, uint32_t handle,
                                  uint32_t tiling_mode)
{
    DLOG("TODO: magma_system_set_tiling_mode");
    return false;
}

bool magma_system_map(magma_system_connection* connection, uint32_t handle, void** paddr)
{
    auto buf = MagmaSystemConnection::cast(connection)->LookupBuffer(handle);
    if (!buf)
        return false;

    return buf->platform_buffer()->MapCpu(paddr);
}

bool magma_system_unmap(magma_system_connection* connection, uint32_t handle, void* addr)
{
    auto buf = MagmaSystemConnection::cast(connection)->LookupBuffer(handle);
    if (!buf)
        return false;

    return buf->platform_buffer()->UnmapCpu();
}

bool magma_system_set_domain(magma_system_connection* connection, uint32_t handle,
                             uint32_t read_domains, uint32_t write_domain)
{
    DLOG("TODO: magma_system_set_domain");
    return false;
}

bool magma_system_submit_command_buffer(struct magma_system_connection* connection,
                                        struct magma_system_command_buffer* command_buffer,
                                        uint32_t context_id)
{
    auto context = MagmaSystemConnection::cast(connection)->LookupContext(context_id);
    if (!context)
        return DRETF(false, "Attempting to execute command buffer on invalid context");

    return context->ExecuteCommandBuffer(command_buffer);
}

void magma_system_wait_rendering(magma_system_connection* connection, uint32_t handle)
{
    DLOG("TODO: magma_system_wait_rendering");
}

///////////////////////////////////////////////////////////////////////////////////

magma_system_display* magma_system_display_open(int32_t fd)
{
    uint32_t device_handle;
    int ioctl_ret = mxio_ioctl(fd, 1, NULL, 0, &device_handle, sizeof(device_handle));
    if (ioctl_ret < 0)
        return DRETP(nullptr, "mxio_ioctl failed: %d", ioctl_ret);

    if (device_handle != 0xdeadbeef)
        return DRETP(nullptr, "Unexpected device_handle");

    MagmaSystemDevice* dev = MagmaDriver::GetDevice();
    DASSERT(dev);

    auto display = dev->OpenDisplay();
    if (!display)
        return DRETP(nullptr, "failed to open display");

    // Here we release ownership of the display to the client
    return display.release();
}

void magma_system_display_close(magma_system_display* display) { delete display; }

bool magma_system_display_import_buffer(magma_system_display* display, uint32_t token,
                                        uint32_t* handle_out)
{
    return MagmaSystemDisplay::cast(display)->ImportBuffer(token, handle_out);
}

void magma_system_display_page_flip(magma_system_display* display, uint32_t handle,
                                    magma_system_pageflip_callback_t callback, void* data)
{
    auto buf = MagmaSystemDisplay::cast(display)->LookupBuffer(handle);
    if (!buf) {
        DLOG("Attempting to page flip with invalid buffer");
        callback(-EINVAL, data);
        return;
    }

    MagmaSystemDisplay::cast(display)->PageFlip(buf, callback, data);
}
