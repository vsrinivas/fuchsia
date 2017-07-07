// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma.h"
#include "magenta/magenta_platform_ioctl.h"
#include "magma_util/command_buffer.h"
#include "magma_util/macros.h"
#include "platform_connection.h"
#include "platform_semaphore.h"
#include "platform_trace.h"
#include <vector>

magma_connection_t* magma_open(int fd, uint32_t capabilities)
{
    magma_system_connection_request request;
    request.client_id = 0;
    request.capabilities = capabilities;

    uint32_t device_handle;
    int ioctl_ret = mxio_ioctl(fd, IOCTL_MAGMA_CONNECT, &request, sizeof(request), &device_handle,
                               sizeof(device_handle));
    if (ioctl_ret < 0)
        return DRETP(nullptr, "mxio_ioctl failed: %d", ioctl_ret);

#if MAGMA_ENABLE_TRACING
    static std::atomic_bool tracing_enabled{false};
    bool value = false;
    if (tracing_enabled.compare_exchange_strong(value, true))
        tracing::InitializeTracer(app::ApplicationContext::CreateFromStartupInfo().get(),
                                  {"libmagma"});
#endif

    // Here we release ownership of the connection to the client
    return magma::PlatformIpcConnection::Create(device_handle).release();
}

void magma_close(magma_connection_t* connection)
{
    // TODO(MA-109): close the connection
    delete magma::PlatformIpcConnection::cast(connection);
}

magma_status_t magma_get_error(magma_connection_t* connection)
{
    return magma::PlatformIpcConnection::cast(connection)->GetError();
}

magma_status_t magma_query(int fd, uint64_t id, uint64_t* value_out)
{
    if (!value_out)
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "bad value_out address");

    int ret = mxio_ioctl(fd, IOCTL_MAGMA_QUERY, &id, sizeof(uint64_t), value_out, sizeof(uint64_t));
    if (ret < 0)
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "mxio_ioctl failed: %d", ret);

    DLOG("magma_query id %" PRIu64 " returned 0x%" PRIx64, id, *value_out);
    return MAGMA_STATUS_OK;
}

void magma_create_context(magma_connection_t* connection, uint32_t* context_id_out)
{
    magma::PlatformIpcConnection::cast(connection)->CreateContext(context_id_out);
}

void magma_destroy_context(magma_connection_t* connection, uint32_t context_id)
{
    magma::PlatformIpcConnection::cast(connection)->DestroyContext(context_id);
}

magma_status_t magma_alloc(magma_connection_t* connection, uint64_t size, uint64_t* size_out,
                           magma_buffer_t* buffer_out)
{
    auto platform_buffer = magma::PlatformBuffer::Create(size, "magma_alloc");
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

void magma_free(magma_connection_t* connection, magma_buffer_t buffer)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);
    magma::PlatformIpcConnection::cast(connection)->ReleaseBuffer(platform_buffer->id());
    delete platform_buffer;
}

uint64_t magma_get_buffer_id(magma_buffer_t buffer)
{
    return reinterpret_cast<magma::PlatformBuffer*>(buffer)->id();
}

uint64_t magma_get_buffer_size(magma_buffer_t buffer)
{
    return reinterpret_cast<magma::PlatformBuffer*>(buffer)->size();
}

magma_status_t magma_import(magma_connection_t* connection, uint32_t buffer_handle,
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

magma_status_t magma_export(magma_connection_t* connection, magma_buffer_t buffer,
                            uint32_t* buffer_handle_out)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);

    if (!platform_buffer->duplicate_handle(buffer_handle_out))
        return DRET(MAGMA_STATUS_INVALID_ARGS);

    return MAGMA_STATUS_OK;
}

magma_status_t magma_map(magma_connection_t* connection, magma_buffer_t buffer, void** addr_out)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);

    if (!platform_buffer->MapCpu(addr_out))
        return DRET(MAGMA_STATUS_MEMORY_ERROR);

    return MAGMA_STATUS_OK;
}

magma_status_t magma_unmap(magma_connection_t* connection, magma_buffer_t buffer)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);

    if (!platform_buffer->UnmapCpu())
        return DRET(MAGMA_STATUS_MEMORY_ERROR);

    return MAGMA_STATUS_OK;
}

magma_status_t magma_alloc_command_buffer(magma_connection_t* connection, uint64_t size,
                                          magma_buffer_t* buffer_out)
{
    auto platform_buffer = magma::PlatformBuffer::Create(size, "magma_command_buffer");
    if (!platform_buffer)
        return DRET(MAGMA_STATUS_MEMORY_ERROR);

    *buffer_out =
        reinterpret_cast<magma_buffer_t>(platform_buffer.release()); // Ownership passed across abi

    return MAGMA_STATUS_OK;
}

void magma_release_command_buffer(struct magma_connection_t* connection,
                                  magma_buffer_t command_buffer)
{
    delete reinterpret_cast<magma::PlatformBuffer*>(command_buffer);
}

void magma_submit_command_buffer(magma_connection_t* connection, magma_buffer_t command_buffer,
                                 uint32_t context_id)
{
    TRACE_DURATION("magma", "submit_command_buffer");

    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(command_buffer);

    class CommandBufferInterpreter : public magma::CommandBuffer {
    public:
        CommandBufferInterpreter(magma::PlatformBuffer* platform_buffer)
            : platform_buffer_(platform_buffer)
        {
        }

        magma::PlatformBuffer* platform_buffer() override { return platform_buffer_; }

    private:
        magma::PlatformBuffer* platform_buffer_;
    };

    CommandBufferInterpreter interpreter(platform_buffer);
    if (!interpreter.Initialize()) {
        DLOG("failed to initialize interpreter");
        return;
    }

    uint32_t buffer_handle;
    if (!platform_buffer->duplicate_handle(&buffer_handle)) {
        DLOG("failed to duplicate handle");
        return;
    }

    uint64_t batch_buffer_id =
        interpreter.resource(interpreter.batch_buffer_resource_index()).buffer_id();
    TRACE_FLOW_BEGIN("magma", "command_buffer", batch_buffer_id);
    (void)batch_buffer_id;

    magma::PlatformIpcConnection::cast(connection)->ExecuteCommandBuffer(buffer_handle, context_id);

    delete platform_buffer;
}

void magma_wait_rendering(magma_connection_t* connection, magma_buffer_t buffer)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);

    magma::PlatformIpcConnection::cast(connection)->WaitRendering(platform_buffer->id());
}

magma_status_t magma_display_get_size(int fd, magma_display_size* size_out)
{
    if (!size_out)
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "bad size_out address");

    int ret = mxio_ioctl(fd, IOCTL_MAGMA_DISPLAY_GET_SIZE, nullptr, 0, size_out, sizeof(*size_out));
    if (ret < 0)
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "mxio_ioctl failed: %d", ret);
    return MAGMA_STATUS_OK;
}

void magma_display_page_flip(magma_connection_t* connection, magma_buffer_t buffer,
                             uint32_t wait_semaphore_count,
                             const magma_semaphore_t* wait_semaphores,
                             uint32_t signal_semaphore_count,
                             const magma_semaphore_t* signal_semaphores)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);

    std::vector<uint64_t> semaphore_ids(wait_semaphore_count + signal_semaphore_count);
    uint32_t index = 0;
    for (uint32_t i = 0; i < wait_semaphore_count; i++) {
        semaphore_ids[index++] = magma_get_semaphore_id(wait_semaphores[i]);
    }
    for (uint32_t i = 0; i < signal_semaphore_count; i++) {
        semaphore_ids[index++] = magma_get_semaphore_id(signal_semaphores[i]);
    }

    magma::PlatformIpcConnection::cast(connection)
        ->PageFlip(platform_buffer->id(), wait_semaphore_count, signal_semaphore_count,
                   semaphore_ids.data());
}

magma_status_t magma_create_semaphore(magma_connection_t* connection,
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

void magma_destroy_semaphore(magma_connection_t* connection, magma_semaphore_t semaphore)
{
    auto platform_semaphore = reinterpret_cast<magma::PlatformSemaphore*>(semaphore);
    magma::PlatformIpcConnection::cast(connection)
        ->ReleaseObject(platform_semaphore->id(), magma::PlatformObject::SEMAPHORE);
    delete platform_semaphore;
}

uint64_t magma_get_semaphore_id(magma_semaphore_t semaphore)
{
    return reinterpret_cast<magma::PlatformSemaphore*>(semaphore)->id();
}

void magma_signal_semaphore(magma_semaphore_t semaphore)
{
    reinterpret_cast<magma::PlatformSemaphore*>(semaphore)->Signal();
}

void magma_reset_semaphore(magma_semaphore_t semaphore)
{
    reinterpret_cast<magma::PlatformSemaphore*>(semaphore)->Reset();
}

magma_status_t magma_wait_semaphore(magma_semaphore_t semaphore, uint64_t timeout)
{
    if (!reinterpret_cast<magma::PlatformSemaphore*>(semaphore)->Wait(timeout))
        return MAGMA_STATUS_TIMED_OUT;

    return MAGMA_STATUS_OK;
}

magma_status_t magma_export_semaphore(magma_connection_t* connection, magma_semaphore_t semaphore,
                                      uint32_t* semaphore_handle_out)
{
    auto platform_semaphore = reinterpret_cast<magma::PlatformSemaphore*>(semaphore);

    if (!platform_semaphore->duplicate_handle(semaphore_handle_out))
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "duplicate_handle failed");

    return MAGMA_STATUS_OK;
}

magma_status_t magma_import_semaphore(magma_connection_t* connection, uint32_t semaphore_handle,
                                      magma_semaphore_t* semaphore_out)
{
    auto platform_semaphore = magma::PlatformSemaphore::Import(semaphore_handle);
    if (!platform_semaphore)
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "PlatformSemaphore::Import failed");

    uint32_t handle;
    if (!platform_semaphore->duplicate_handle(&handle))
        return DRET_MSG(MAGMA_STATUS_ACCESS_DENIED, "failed to duplicate handle");

    magma_status_t result = magma::PlatformIpcConnection::cast(connection)
                                ->ImportObject(handle, magma::PlatformObject::SEMAPHORE);
    if (result != MAGMA_STATUS_OK)
        return DRET_MSG(result, "ImportObject failed: %d", result);

    *semaphore_out = reinterpret_cast<magma_semaphore_t>(platform_semaphore.release());

    return MAGMA_STATUS_OK;
}
