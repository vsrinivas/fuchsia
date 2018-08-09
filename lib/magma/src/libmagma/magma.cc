// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma.h"
#include "magma_util/command_buffer.h"
#include "magma_util/macros.h"
#include "platform_connection.h"
#include "platform_port.h"
#include "platform_semaphore.h"
#include "platform_thread.h"
#include "platform_trace.h"
#include "zircon/zircon_platform_ioctl.h"
#include <chrono>
#include <vector>

magma_connection_t* magma_create_connection(int fd, uint32_t capabilities)
{

    magma_system_connection_request request;
    request.client_id = magma::PlatformThreadId().id();
    request.capabilities = capabilities;

    uint32_t device_handles[2];
    int ioctl_ret = fdio_ioctl(fd, IOCTL_MAGMA_CONNECT, &request, sizeof(request), device_handles,
                               sizeof(device_handles));
    if (ioctl_ret < 0)
        return DRETP(nullptr, "fdio_ioctl failed: %d", ioctl_ret);

    // Here we release ownership of the connection to the client
    return magma::PlatformIpcConnection::Create(device_handles[0], device_handles[1]).release();
}

void magma_release_connection(magma_connection_t* connection)
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

    int ret = fdio_ioctl(fd, IOCTL_MAGMA_QUERY, &id, sizeof(uint64_t), value_out, sizeof(uint64_t));
    if (ret < 0)
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "fdio_ioctl failed: %d", ret);

    DLOG("magma_query id %" PRIu64 " returned 0x%" PRIx64, id, *value_out);
    return MAGMA_STATUS_OK;
}

void magma_create_context(magma_connection_t* connection, uint32_t* context_id_out)
{
    magma::PlatformIpcConnection::cast(connection)->CreateContext(context_id_out);
}

void magma_release_context(magma_connection_t* connection, uint32_t context_id)
{
    magma::PlatformIpcConnection::cast(connection)->DestroyContext(context_id);
}

magma_status_t magma_create_buffer(magma_connection_t* connection, uint64_t size,
                                   uint64_t* size_out, magma_buffer_t* buffer_out)
{
    auto platform_buffer = magma::PlatformBuffer::Create(size, "magma_create_buffer");
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

void magma_release_buffer(magma_connection_t* connection, magma_buffer_t buffer)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);
    magma::PlatformIpcConnection::cast(connection)->ReleaseBuffer(platform_buffer->id());
    delete platform_buffer;
}

magma_status_t magma_set_cache_policy(magma_buffer_t buffer, magma_cache_policy_t policy)
{
    bool result = reinterpret_cast<magma::PlatformBuffer*>(buffer)->SetCachePolicy(policy);
    return result ? MAGMA_STATUS_OK : MAGMA_STATUS_INTERNAL_ERROR;
}

uint64_t magma_get_buffer_id(magma_buffer_t buffer)
{
    return reinterpret_cast<magma::PlatformBuffer*>(buffer)->id();
}

uint64_t magma_get_buffer_size(magma_buffer_t buffer)
{
    return reinterpret_cast<magma::PlatformBuffer*>(buffer)->size();
}

int magma_get_notification_channel_fd(magma_connection_t* connection)
{
    return magma::PlatformIpcConnection::cast(connection)->GetNotificationChannelFd();
}

magma_status_t magma_read_notification_channel(magma_connection_t* connection, void* buffer,
                                               uint64_t buffer_size, uint64_t* buffer_size_out)
{
    return magma::PlatformIpcConnection::cast(connection)
        ->ReadNotificationChannel(buffer, buffer_size, buffer_size_out);
}

magma_status_t magma_clean_cache(magma_buffer_t buffer, uint64_t offset, uint64_t size,
                                 magma_cache_operation_t operation)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);
    bool invalidate;
    switch (operation) {
        case MAGMA_CACHE_OPERATION_CLEAN:
            invalidate = false;
            break;
        case MAGMA_CACHE_OPERATION_CLEAN_INVALIDATE:
            invalidate = true;
            break;
        default:
            return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "invalid cache operations");
    }

    bool result = platform_buffer->CleanCache(offset, size, invalidate);
    return result ? MAGMA_STATUS_OK : MAGMA_STATUS_INTERNAL_ERROR;
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

magma_status_t magma_map_aligned(magma_connection_t* connection, magma_buffer_t buffer,
                                 uint64_t alignment, void** addr_out)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);

    if (!platform_buffer->MapCpu(addr_out, alignment))
        return DRET(MAGMA_STATUS_MEMORY_ERROR);

    return MAGMA_STATUS_OK;
}

magma_status_t magma_map_specific(magma_connection_t* connection, magma_buffer_t buffer,
                                  uint64_t addr)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);

    if (!platform_buffer->MapAtCpuAddr(addr))
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

void magma_map_buffer_gpu(struct magma_connection_t* connection, magma_buffer_t buffer,
                          uint64_t page_offset, uint64_t page_count, uint64_t gpu_va,
                          uint64_t map_flags)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);
    uint64_t buffer_id = platform_buffer->id();
    magma::PlatformIpcConnection::cast(connection)
        ->MapBufferGpu(buffer_id, gpu_va, page_offset, page_count, map_flags);
}

void magma_unmap_buffer_gpu(struct magma_connection_t* connection, magma_buffer_t buffer,
                            uint64_t gpu_va)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);
    uint64_t buffer_id = platform_buffer->id();
    magma::PlatformIpcConnection::cast(connection)->UnmapBufferGpu(buffer_id, gpu_va);
}

void magma_commit_buffer(struct magma_connection_t* connection, magma_buffer_t buffer,
                         uint64_t page_offset, uint64_t page_count)
{
    auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);
    uint64_t buffer_id = platform_buffer->id();
    magma::PlatformIpcConnection::cast(connection)
        ->CommitBuffer(buffer_id, page_offset, page_count);
}

magma_status_t magma_create_command_buffer(magma_connection_t* connection, uint64_t size,
                                           magma_buffer_t* buffer_out)
{
    auto platform_buffer = magma::PlatformBuffer::Create(size, "magma_command_buffer");
    if (!platform_buffer)
        return DRET(MAGMA_STATUS_MEMORY_ERROR);

    // Mapping can be expensive (mostly due to aslr);
    // and we want to map this buffer on submit to pull out the batch buffer id (see below).
    // To avoid mapping twice, we do an initial map here, so that when the client does map-unmap
    // then submits, the mapping will probably still be alive inside the platform buffer.
    void* data;
    platform_buffer->MapCpu(&data);

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

    uint64_t ATTRIBUTE_UNUSED batch_buffer_id =
        interpreter.resource(interpreter.batch_buffer_resource_index()).buffer_id();
    TRACE_FLOW_BEGIN("magma", "command_buffer", batch_buffer_id);

    magma::PlatformIpcConnection::cast(connection)->ExecuteCommandBuffer(buffer_handle, context_id);

    delete platform_buffer;
}

void magma_execute_immediate_commands(magma_connection_t* connection, uint32_t context_id,
                                      uint64_t command_count,
                                      magma_system_inline_command_buffer* command_buffers)
{
    magma::PlatformIpcConnection::cast(connection)
        ->ExecuteImmediateCommands(context_id, command_count, command_buffers);
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

void magma_release_semaphore(magma_connection_t* connection, magma_semaphore_t semaphore)
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

magma_status_t magma_wait_semaphores(const magma_semaphore_t* semaphores, uint32_t count,
                                     uint64_t timeout_ms, magma_bool_t wait_all)
{
    if (count == 1) {
        if (!reinterpret_cast<magma::PlatformSemaphore*>(semaphores[0])->WaitNoReset(timeout_ms))
            return MAGMA_STATUS_TIMED_OUT;
        return MAGMA_STATUS_OK;
    }

    std::unique_ptr<magma::PlatformPort> port = magma::PlatformPort::Create();
    for (uint32_t i = 0; i < count; i++) {
        if (!reinterpret_cast<magma::PlatformSemaphore*>(semaphores[i])->WaitAsync(port.get()))
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "WaitAsync failed");
    }

    if (!wait_all) {
        uint64_t key;
        return port->Wait(&key, timeout_ms).get();
    }

    auto end_time = timeout_ms == UINT64_MAX
                        ? std::chrono::steady_clock::time_point::max()
                        : std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    for (uint32_t i = 0; i < count; i++) {
        uint64_t key;
        auto time_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - std::chrono::steady_clock::now());
        magma::Status status =
            port->Wait(&key, time_remaining.count() > 0 ? time_remaining.count() : 0);
        if (!status)
            return status.get();
    }
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
