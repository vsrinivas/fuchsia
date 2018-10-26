// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_connection.h"

#include <fuchsia/gpu/magma/c/fidl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/unsafe.h>
#include <lib/zx/channel.h>

namespace magma {

class ZirconPlatformIpcConnection : public PlatformIpcConnection {
public:
    ZirconPlatformIpcConnection(zx::channel channel, zx::channel notification_channel)
        : channel_(std::move(channel)), notification_channel_(std::move(notification_channel))
    {
    }

    // Imports a buffer for use in the system driver
    magma_status_t ImportBuffer(PlatformBuffer* buffer) override
    {
        if (!buffer)
            return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "attempting to import null buffer");

        uint32_t duplicate_handle;
        if (!buffer->duplicate_handle(&duplicate_handle))
            return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "failed to get duplicate_handle");

        zx_handle_t duplicate_handle_zx = duplicate_handle;

        ImportBufferOp op;
        magma_status_t result = channel_write(&op, sizeof(op), &duplicate_handle_zx, 1);
        if (result != MAGMA_STATUS_OK) {
            return DRET_MSG(result, "failed to write to channel");
        }

        return MAGMA_STATUS_OK;
    }

    // Destroys the buffer with |buffer_id| within this connection
    // returns false if the buffer with |buffer_id| has not been imported
    magma_status_t ReleaseBuffer(uint64_t buffer_id) override
    {
        ReleaseBufferOp op;
        op.buffer_id = buffer_id;
        magma_status_t result = channel_write(&op, sizeof(op), nullptr, 0);
        if (result != MAGMA_STATUS_OK)
            return DRET_MSG(result, "failed to write to channel");

        return MAGMA_STATUS_OK;
    }

    magma_status_t ImportObject(uint32_t handle, PlatformObject::Type object_type) override
    {
        zx_handle_t duplicate_handle_zx = handle;

        ImportObjectOp op;
        op.object_type = object_type;

        magma_status_t result = channel_write(&op, sizeof(op), &duplicate_handle_zx, 1);
        if (result != MAGMA_STATUS_OK) {
            return DRET_MSG(result, "failed to write to channel");
        }

        return MAGMA_STATUS_OK;
    }

    magma_status_t ReleaseObject(uint64_t object_id, PlatformObject::Type object_type) override
    {
        ReleaseObjectOp op;
        op.object_id = object_id;
        op.object_type = object_type;
        magma_status_t result = channel_write(&op, sizeof(op), nullptr, 0);
        if (result != MAGMA_STATUS_OK)
            return DRET_MSG(result, "failed to write to channel");

        return MAGMA_STATUS_OK;
    }

    // Creates a context and returns the context id
    void CreateContext(uint32_t* context_id_out) override
    {
        auto context_id = next_context_id_++;
        *context_id_out = context_id;

        CreateContextOp op;
        op.context_id = context_id;
        magma_status_t result = channel_write(&op, sizeof(op), nullptr, 0);
        if (result != MAGMA_STATUS_OK)
            SetError(result);
    }

    // Destroys a context for the given id
    void DestroyContext(uint32_t context_id) override
    {
        DestroyContextOp op;
        op.context_id = context_id;
        magma_status_t result = channel_write(&op, sizeof(op), nullptr, 0);
        if (result != MAGMA_STATUS_OK)
            SetError(result);
    }

    void ExecuteCommandBuffer(uint32_t command_buffer_handle, uint32_t context_id) override
    {
        ExecuteCommandBufferOp op;
        op.context_id = context_id;

        zx_handle_t duplicate_handle_zx = command_buffer_handle;
        magma_status_t result = channel_write(&op, sizeof(op), &duplicate_handle_zx, 1);
        if (result != MAGMA_STATUS_OK) {
            SetError(result);
        }
    }

    void ExecuteImmediateCommands(uint32_t context_id, uint64_t command_count,
                                  magma_system_inline_command_buffer* command_buffers) override
    {
        uint8_t payload[kReceiveBufferSize];
        uint64_t commands_sent = 0;
        while (commands_sent < command_count) {
            auto op = new (payload) ExecuteImmediateCommandsOp;
            op->context_id = context_id;

            uint64_t space_used = sizeof(ExecuteImmediateCommandsOp);
            uint64_t semaphores_used = 0;
            uint64_t last_command;
            for (last_command = commands_sent; last_command < command_count; last_command++) {
                uint64_t command_space =
                    command_buffers[last_command].size +
                    command_buffers[last_command].semaphore_count * sizeof(uint64_t);
                space_used += command_space;
                if (space_used > sizeof(payload))
                    break;
                semaphores_used += command_buffers[last_command].semaphore_count;
            }

            op->semaphore_count = semaphores_used;

            uint64_t* semaphore_data = op->semaphores;
            uint8_t* command_data = op->command_data();
            uint64_t command_data_used = 0;
            for (uint64_t i = commands_sent; i < last_command; i++) {
                memcpy(semaphore_data, command_buffers[i].semaphores,
                       command_buffers[i].semaphore_count * sizeof(uint64_t));
                semaphore_data += command_buffers[i].semaphore_count;
                memcpy(command_data, command_buffers[i].data, command_buffers[i].size);
                command_data += command_buffers[i].size;
                command_data_used += command_buffers[i].size;
            }
            op->commands_size = command_data_used;
            commands_sent = last_command;
            uint64_t payload_size =
                ExecuteImmediateCommandsOp::size(op->semaphore_count, op->commands_size);
            DASSERT(payload_size <= sizeof(payload));
            magma_status_t result = channel_write(payload, payload_size, nullptr, 0);
            if (result != MAGMA_STATUS_OK)
                SetError(result);
        }
    }

    magma_status_t GetError() override
    {
        magma_status_t result = error_;
        error_ = 0;
        if (result != MAGMA_STATUS_OK)
            return result;

        GetErrorOp op;
        result = channel_write(&op, sizeof(op), nullptr, 0);
        if (result != MAGMA_STATUS_OK)
            return DRET_MSG(result, "failed to write to channel");

        magma_status_t error;
        result = WaitError(&error);
        if (result != MAGMA_STATUS_OK)
            return result;

        return error;
    }

    magma_status_t MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va, uint64_t page_offset,
                                uint64_t page_count, uint64_t flags) override
    {
        MapBufferGpuOp op;
        op.buffer_id = buffer_id;
        op.gpu_va = gpu_va;
        op.page_offset = page_offset;
        op.page_count = page_count;
        op.flags = flags;
        magma_status_t result = channel_write(&op, sizeof(op), nullptr, 0);
        if (result != MAGMA_STATUS_OK)
            return DRET_MSG(result, "failed to write to channel");

        return MAGMA_STATUS_OK;
    }

    magma_status_t UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va) override
    {
        UnmapBufferGpuOp op;
        op.buffer_id = buffer_id;
        op.gpu_va = gpu_va;
        magma_status_t result = channel_write(&op, sizeof(op), nullptr, 0);
        if (result != MAGMA_STATUS_OK)
            return DRET_MSG(result, "failed to write to channel");

        return MAGMA_STATUS_OK;
    }

    magma_status_t CommitBuffer(uint64_t buffer_id, uint64_t page_offset,
                                uint64_t page_count) override
    {
        CommitBufferOp op;
        op.buffer_id = buffer_id;
        op.page_offset = page_offset;
        op.page_count = page_count;
        magma_status_t result = channel_write(&op, sizeof(op), nullptr, 0);
        if (result != MAGMA_STATUS_OK)
            return DRET_MSG(result, "failed to write to channel");

        return MAGMA_STATUS_OK;
    }

    void SetError(magma_status_t error)
    {
        if (!error_)
            error_ = DRET_MSG(error, "ZirconPlatformIpcConnection encountered dispatcher error");
    }

    magma_status_t WaitError(magma_status_t* error_out)
    {
        return WaitMessage(reinterpret_cast<uint8_t*>(error_out), sizeof(*error_out), true);
    }

    magma_status_t WaitMessage(uint8_t* msg_out, uint32_t msg_size, bool blocking)
    {
        zx_signals_t signals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
        zx_signals_t pending = 0;

        zx_status_t status =
            channel_.wait_one(signals, blocking ? zx::time::infinite() : zx::time(), &pending);
        if (status == ZX_ERR_TIMED_OUT) {
            DLOG("got ZX_ERR_TIMED_OUT, returning true");
            return 0;
        } else if (status == ZX_OK) {
            DLOG("got ZX_OK, blocking: %s, readable: %s, closed %s", blocking ? "true" : "false",
                 pending & ZX_CHANNEL_READABLE ? "true" : "false",
                 pending & ZX_CHANNEL_PEER_CLOSED ? "true" : "false");
            if (pending & ZX_CHANNEL_READABLE) {
                uint32_t actual_bytes;
                zx_status_t status =
                    channel_.read(0, msg_out, msg_size, &actual_bytes, nullptr, 0, nullptr);
                if (status != ZX_OK)
                    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to read from channel");
                if (actual_bytes != msg_size)
                    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR,
                                    "read wrong number of bytes from channel");
            } else if (pending & ZX_CHANNEL_PEER_CLOSED) {
                return DRET_MSG(MAGMA_STATUS_CONNECTION_LOST, "channel, closed");
            }
            return 0;
        } else {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to wait on channel");
        }
    }

    int GetNotificationChannelFd() override
    {
        return fdio_handle_fd(notification_channel_.get(),
                              ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, 0, true);
    }

    magma_status_t ReadNotificationChannel(void* buffer, size_t buffer_size,
                                           size_t* buffer_size_out) override
    {
        uint32_t buffer_actual_size;
        zx_status_t status = notification_channel_.read(0, buffer, buffer_size, &buffer_actual_size,
                                                        nullptr, 0, nullptr);
        *buffer_size_out = buffer_actual_size;
        if (status == ZX_ERR_SHOULD_WAIT) {
            *buffer_size_out = 0;
            return MAGMA_STATUS_OK;
        } else if (status == ZX_OK) {
            return MAGMA_STATUS_OK;
        } else if (status == ZX_ERR_PEER_CLOSED) {
            return DRET_MSG(MAGMA_STATUS_CONNECTION_LOST, "channel, closed");
        } else {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to wait on channel status %u",
                            status);
        }
    }

private:
    magma_status_t channel_write(const void* bytes, uint32_t num_bytes, const zx_handle_t* handles,
                                 uint32_t num_handles)
    {
        zx_status_t status = channel_.write(0, bytes, num_bytes, handles, num_handles);
        switch (status) {
            case ZX_OK:
                return MAGMA_STATUS_OK;
            case ZX_ERR_PEER_CLOSED:
                return MAGMA_STATUS_CONNECTION_LOST;
            default:
                return MAGMA_STATUS_INTERNAL_ERROR;
        }
    }

    zx::channel channel_;
    zx::channel notification_channel_;
    uint32_t next_context_id_ = 1;
    magma_status_t error_{};
};

std::unique_ptr<PlatformIpcConnection>
PlatformIpcConnection::Create(uint32_t device_handle, uint32_t device_notification_handle)
{
    return std::unique_ptr<ZirconPlatformIpcConnection>(new ZirconPlatformIpcConnection(
        zx::channel(device_handle), zx::channel(device_notification_handle)));
}

bool PlatformIpcConnection::Query(int fd, uint64_t query_id, uint64_t* result_out)
{
    fdio_t* fdio = fdio_unsafe_fd_to_io(fd);
    if (!fdio)
        return DRETF(false, "invalid fd: %d", fd);

    zx_status_t status =
        fuchsia_gpu_magma_DeviceQuery(fdio_unsafe_borrow_channel(fdio), query_id, result_out);
    fdio_unsafe_release(fdio);

    if (status != ZX_OK)
        return DRETF(false, "magma_DeviceQuery failed: %d", status);

    return true;
}

bool PlatformIpcConnection::GetHandles(int fd, uint32_t* device_handle_out,
                                       uint32_t* device_notification_handle_out)
{
    fdio_t* fdio = fdio_unsafe_fd_to_io(fd);
    if (!fdio)
        return DRETF(false, "invalid fd: %d", fd);

    zx_status_t status = fuchsia_gpu_magma_DeviceConnect(
        fdio_unsafe_borrow_channel(fdio), magma::PlatformThreadId().id(), device_handle_out,
        device_notification_handle_out);
    fdio_unsafe_release(fdio);

    if (status != ZX_OK)
        return DRETF(false, "magma_DeviceConnect failed: %d", status);

    return true;
}

} // namespace magma
