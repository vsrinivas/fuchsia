// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_connection.h"
#include "zircon_platform_event.h"

#include <list>

#include <fdio/io.h>
#include <lib/zx/channel.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

namespace magma {

constexpr size_t kReceiveBufferSize = 256;

enum OpCode {
    ImportBuffer,
    ReleaseBuffer,
    ImportObject,
    ReleaseObject,
    CreateContext,
    DestroyContext,
    ExecuteCommandBuffer,
    WaitRendering,
    GetError,
    MapBufferGpu,
    UnmapBufferGpu,
    CommitBuffer,
    ExecuteImmediateCommands,
};

struct ImportBufferOp {
    const OpCode opcode = ImportBuffer;
    static constexpr uint32_t kNumHandles = 1;
} __attribute__((packed));

struct ReleaseBufferOp {
    const OpCode opcode = ReleaseBuffer;
    static constexpr uint32_t kNumHandles = 0;
    uint64_t buffer_id;
} __attribute__((packed));

struct ImportObjectOp {
    const OpCode opcode = ImportObject;
    uint32_t object_type;
    static constexpr uint32_t kNumHandles = 1;
} __attribute__((packed));

struct ReleaseObjectOp {
    const OpCode opcode = ReleaseObject;
    static constexpr uint32_t kNumHandles = 0;
    uint64_t object_id;
    uint32_t object_type;
} __attribute__((packed));

struct CreateContextOp {
    const OpCode opcode = CreateContext;
    static constexpr uint32_t kNumHandles = 0;
    uint32_t context_id;
} __attribute__((packed));

struct DestroyContextOp {
    const OpCode opcode = DestroyContext;
    static constexpr uint32_t kNumHandles = 0;
    uint32_t context_id;
} __attribute__((packed));

struct ExecuteCommandBufferOp {
    const OpCode opcode = ExecuteCommandBuffer;
    static constexpr uint32_t kNumHandles = 1;
    uint32_t context_id;
} __attribute__((packed));

struct WaitRenderingOp {
    const OpCode opcode = WaitRendering;
    static constexpr uint32_t kNumHandles = 0;
    uint64_t buffer_id;
} __attribute__((packed));

struct ExecuteImmediateCommandsOp {
    const OpCode opcode = ExecuteImmediateCommands;
    static constexpr uint32_t kNumHandles = 0;
    uint32_t context_id;
    uint32_t semaphore_count;
    uint32_t commands_size;
    uint64_t semaphores[];
    // Command data follows the last semaphore.

    uint8_t* command_data() { return reinterpret_cast<uint8_t*>(&semaphores[semaphore_count]); }

    static uint32_t size(uint32_t semaphore_count, uint32_t commands_size)
    {
        return sizeof(ExecuteImmediateCommandsOp) + commands_size +
               sizeof(uint64_t) * semaphore_count;
    }
} __attribute__((packed));

struct GetErrorOp {
    const OpCode opcode = GetError;
    static constexpr uint32_t kNumHandles = 0;
} __attribute__((packed));

struct MapBufferGpuOp {
    const OpCode opcode = MapBufferGpu;
    static constexpr uint32_t kNumHandles = 0;
    uint64_t buffer_id;
    uint64_t gpu_va;
    uint64_t page_offset;
    uint64_t page_count;
    uint64_t flags;
} __attribute__((packed));

struct UnmapBufferGpuOp {
    const OpCode opcode = UnmapBufferGpu;
    static constexpr uint32_t kNumHandles = 0;
    uint64_t buffer_id;
    uint64_t gpu_va;
} __attribute__((packed));

struct CommitBufferOp {
    const OpCode opcode = CommitBuffer;
    static constexpr uint32_t kNumHandles = 0;
    uint64_t buffer_id;
    uint64_t page_offset;
    uint64_t page_count;
} __attribute__((packed));

template <typename T>
T* OpCast(uint8_t* bytes, uint32_t num_bytes, zx_handle_t* handles, uint32_t kNumHandles)
{
    if (num_bytes != sizeof(T))
        return DRETP(nullptr, "wrong number of bytes in message, expected %zu, got %u", sizeof(T),
                     num_bytes);
    if (kNumHandles != T::kNumHandles)
        return DRETP(nullptr, "wrong number of handles in message");
    return reinterpret_cast<T*>(bytes);
}

template <>
ExecuteImmediateCommandsOp* OpCast<ExecuteImmediateCommandsOp>(uint8_t* bytes, uint32_t num_bytes,
                                                               zx_handle_t* handles,
                                                               uint32_t kNumHandles)
{
    if (num_bytes < sizeof(ExecuteImmediateCommandsOp))
        return DRETP(nullptr, "too few bytes for executing immediate commands %u", num_bytes);

    auto execute_immediate_commands_op = reinterpret_cast<ExecuteImmediateCommandsOp*>(bytes);
    uint32_t expected_size =
        ExecuteImmediateCommandsOp::size(execute_immediate_commands_op->semaphore_count,
                                         execute_immediate_commands_op->commands_size);
    if (num_bytes != expected_size)
        return DRETP(nullptr, "wrong number of bytes in message, expected %u, got %u",
                     expected_size, num_bytes);
    if (kNumHandles != ExecuteImmediateCommandsOp::kNumHandles)
        return DRETP(nullptr, "wrong number of handles in message");
    return reinterpret_cast<ExecuteImmediateCommandsOp*>(bytes);
}

class ZirconPlatformConnection : public PlatformConnection,
                                  public std::enable_shared_from_this<ZirconPlatformConnection> {
public:
    ZirconPlatformConnection(std::unique_ptr<Delegate> delegate, zx::channel local_endpoint,
                             zx::channel remote_endpoint, zx::channel local_notification_endpoint,
                             zx::channel remote_notification_endpoint,
                             std::unique_ptr<magma::PlatformEvent> shutdown_event)
        : magma::PlatformConnection(std::move(shutdown_event)), delegate_(std::move(delegate)),
          local_endpoint_(std::move(local_endpoint)), remote_endpoint_(std::move(remote_endpoint)),
          local_notification_endpoint_(std::move(local_notification_endpoint)),
          remote_notification_endpoint_(std::move(remote_notification_endpoint))
    {
    }

    ~ZirconPlatformConnection() { delegate_->SetNotificationChannel(nullptr, 0); }

    bool HandleRequest() override
    {
        constexpr uint32_t num_bytes = kReceiveBufferSize;
        constexpr uint32_t kNumHandles = 1;

        uint32_t actual_bytes;
        uint32_t actual_handles;

        uint8_t bytes[num_bytes];
        zx_handle_t handles[kNumHandles];

        auto shutdown_event = static_cast<ZirconPlatformEvent*>(ShutdownEvent().get());

        constexpr uint32_t kIndexChannel = 0;
        constexpr uint32_t kIndexShutdown = 1;

        constexpr uint32_t wait_item_count = 2;
        zx_wait_item_t wait_items[wait_item_count];
        wait_items[kIndexChannel] = {local_endpoint_.get(),
                                      ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, 0};
        wait_items[kIndexShutdown] = {shutdown_event->zx_handle(), shutdown_event->zx_signal(), 0};

        if (zx_object_wait_many(wait_items, wait_item_count, ZX_TIME_INFINITE) != ZX_OK)
            return DRETF(false, "wait_many failed");

        if (wait_items[kIndexShutdown].pending & shutdown_event->zx_signal())
            return DRETF(false, "shutdown event signalled");

        if (wait_items[kIndexChannel].pending & ZX_CHANNEL_PEER_CLOSED)
            return false; // No DRET because this happens on the normal connection closed path

        if (wait_items[kIndexChannel].pending & ZX_CHANNEL_READABLE) {
            auto status = local_endpoint_.read(0, bytes, num_bytes, &actual_bytes, handles,
                                               kNumHandles, &actual_handles);
            if (status != ZX_OK)
                return DRETF(false, "failed to read from channel");

            if (actual_bytes < sizeof(OpCode))
                return DRETF(false, "malformed message");

            OpCode* opcode = reinterpret_cast<OpCode*>(bytes);
            bool success = false;
            switch (*opcode) {
                case OpCode::ImportBuffer:
                    success = ImportBuffer(
                        OpCast<ImportBufferOp>(bytes, actual_bytes, handles, actual_handles),
                        handles);
                    break;
                case OpCode::ReleaseBuffer:
                    success = ReleaseBuffer(
                        OpCast<ReleaseBufferOp>(bytes, actual_bytes, handles, actual_handles));
                    break;
                case OpCode::ImportObject:
                    success = ImportObject(
                        OpCast<ImportObjectOp>(bytes, actual_bytes, handles, actual_handles),
                        handles);
                    break;
                case OpCode::ReleaseObject:
                    success = ReleaseObject(
                        OpCast<ReleaseObjectOp>(bytes, actual_bytes, handles, actual_handles));
                    break;
                case OpCode::CreateContext:
                    success = CreateContext(
                        OpCast<CreateContextOp>(bytes, actual_bytes, handles, actual_handles));
                    break;
                case OpCode::DestroyContext:
                    success = DestroyContext(
                        OpCast<DestroyContextOp>(bytes, actual_bytes, handles, actual_handles));
                    break;
                case OpCode::ExecuteCommandBuffer:
                    success =
                        ExecuteCommandBuffer(OpCast<ExecuteCommandBufferOp>(
                                                 bytes, actual_bytes, handles, actual_handles),
                                             handles);
                    break;
                case OpCode::WaitRendering:
                    success = WaitRendering(
                        OpCast<WaitRenderingOp>(bytes, actual_bytes, handles, actual_handles));
                    break;
                case OpCode::ExecuteImmediateCommands:
                    success = ExecuteImmediateCommands(OpCast<ExecuteImmediateCommandsOp>(
                        bytes, actual_bytes, handles, actual_handles));
                    break;
                case OpCode::GetError:
                    success =
                        GetError(OpCast<GetErrorOp>(bytes, actual_bytes, handles, actual_handles));
                    break;
                case OpCode::MapBufferGpu:
                    success = MapBufferGpu(
                        OpCast<MapBufferGpuOp>(bytes, actual_bytes, handles, actual_handles));
                    break;
                case OpCode::UnmapBufferGpu:
                    success = UnmapBufferGpu(
                        OpCast<UnmapBufferGpuOp>(bytes, actual_bytes, handles, actual_handles));
                    break;
                case OpCode::CommitBuffer:
                    success = CommitBuffer(
                        OpCast<CommitBufferOp>(bytes, actual_bytes, handles, actual_handles));
                    break;
            }

            if (!success)
                return DRETF(false, "failed to interpret message");
        }

        if (error_)
            return DRETF(false, "PlatformConnection encountered fatal error");

        return true;
    }

    uint32_t GetHandle() override
    {
        DASSERT(remote_endpoint_);
        return remote_endpoint_.release();
    }

    uint32_t GetNotificationChannel() override
    {
        DASSERT(remote_notification_endpoint_);
        return remote_notification_endpoint_.release();
    }

private:
    bool ImportBuffer(ImportBufferOp* op, zx_handle_t* handle)
    {
        DLOG("Operation: ImportBuffer");
        if (!op)
            return DRETF(false, "malformed message");
        uint64_t buffer_id;
        if (!delegate_->ImportBuffer(*handle, &buffer_id))
            SetError(MAGMA_STATUS_INVALID_ARGS);
        return true;
    }

    bool ReleaseBuffer(ReleaseBufferOp* op)
    {
        DLOG("Operation: ReleaseBuffer");
        if (!op)
            return DRETF(false, "malformed message");
        if (!delegate_->ReleaseBuffer(op->buffer_id))
            SetError(MAGMA_STATUS_INVALID_ARGS);
        return true;
    }

    bool ImportObject(ImportObjectOp* op, zx_handle_t* handle)
    {
        DLOG("Operation: ImportObject");
        if (!op)
            return DRETF(false, "malformed message");
        if (!delegate_->ImportObject(*handle, static_cast<PlatformObject::Type>(op->object_type)))
            SetError(MAGMA_STATUS_INVALID_ARGS);
        return true;
    }

    bool ReleaseObject(ReleaseObjectOp* op)
    {
        DLOG("Operation: ReleaseObject");
        if (!op)
            return DRETF(false, "malformed message");
        if (!delegate_->ReleaseObject(op->object_id,
                                      static_cast<PlatformObject::Type>(op->object_type)))
            SetError(MAGMA_STATUS_INVALID_ARGS);
        return true;
    }

    bool CreateContext(CreateContextOp* op)
    {
        DLOG("Operation: CreateContext");
        if (!op)
            return DRETF(false, "malformed message");
        if (!delegate_->CreateContext(op->context_id))
            SetError(MAGMA_STATUS_INTERNAL_ERROR);
        return true;
    }

    bool DestroyContext(DestroyContextOp* op)
    {
        DLOG("Operation: DestroyContext");
        if (!op)
            return DRETF(false, "malformed message");
        if (!delegate_->DestroyContext(op->context_id))
            SetError(MAGMA_STATUS_INTERNAL_ERROR);
        return true;
    }

    bool ExecuteCommandBuffer(ExecuteCommandBufferOp* op, zx_handle_t* handle)
    {
        DLOG("Operation: ExecuteCommandBuffer");
        if (!op)
            return DRETF(false, "malformed message");
        magma::Status status = delegate_->ExecuteCommandBuffer(*handle, op->context_id);
        if (status.get() == MAGMA_STATUS_CONTEXT_KILLED)
            ShutdownEvent()->Signal();
        if (!status)
            SetError(MAGMA_STATUS_INTERNAL_ERROR);
        return true;
    }

    bool WaitRendering(WaitRenderingOp* op)
    {
        DLOG("Operation: WaitRendering");
        if (!op)
            return DRETF(false, "malformed message");
        magma::Status status = delegate_->WaitRendering(op->buffer_id);
        if (status.get() == MAGMA_STATUS_CONTEXT_KILLED)
            ShutdownEvent()->Signal();
        if (!status)
            SetError(MAGMA_STATUS_INTERNAL_ERROR);
        if (!WriteError(0))
            return false;
        return true;
    }

    bool ExecuteImmediateCommands(ExecuteImmediateCommandsOp* op)
    {
        DLOG("Operation: ExecuteImmediateCommands");
        if (!op)
            return DRETF(false, "malformed message");

        magma::Status status = delegate_->ExecuteImmediateCommands(
            op->context_id, op->commands_size, op->command_data(), op->semaphore_count,
            op->semaphores);
        if (!status)
            SetError(status.get());
        return true;
    }

    bool GetError(GetErrorOp* op)
    {
        DLOG("Operation: GetError");
        if (!op)
            return DRETF(false, "malformed message");
        magma_status_t result = error_;
        error_ = 0;
        if (!WriteError(result))
            return false;
        return true;
    }

    bool MapBufferGpu(MapBufferGpuOp* op)
    {
        DLOG("Operation: MapBufferGpu");
        if (!op)
            return DRETF(false, "malformed message");
        if (!delegate_->MapBufferGpu(op->buffer_id, op->gpu_va, op->page_offset, op->page_count,
                                     op->flags))
            SetError(MAGMA_STATUS_INVALID_ARGS);
        return true;
    }

    bool UnmapBufferGpu(UnmapBufferGpuOp* op)
    {
        DLOG("Operation: UnmapBufferGpu");
        if (!op)
            return DRETF(false, "malformed message");
        if (!delegate_->UnmapBufferGpu(op->buffer_id, op->gpu_va))
            SetError(MAGMA_STATUS_INVALID_ARGS);
        return true;
    }

    bool CommitBuffer(CommitBufferOp* op)
    {
        DLOG("Operation: CommitBuffer");
        if (!op)
            return DRETF(false, "malformed message");
        if (!delegate_->CommitBuffer(op->buffer_id, op->page_offset, op->page_count))
            SetError(MAGMA_STATUS_INVALID_ARGS);
        return true;
    }

    void SetError(magma_status_t error)
    {
        if (!error_)
            error_ = DRET_MSG(error, "ZirconPlatformConnection encountered async error");
    }

    bool WriteError(magma_status_t error)
    {
        DLOG("Writing error %d to channel", error);
        auto status = local_endpoint_.write(0, &error, sizeof(error), nullptr, 0);
        return DRETF(status == ZX_OK, "failed to write to channel");
    }

    std::unique_ptr<Delegate> delegate_;
    zx::channel local_endpoint_;
    zx::channel remote_endpoint_;
    magma_status_t error_{};
    zx::channel local_notification_endpoint_;
    zx::channel remote_notification_endpoint_;
};

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
            zx_handle_close(duplicate_handle);
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
            zx_handle_close(handle);
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
            zx_handle_close(command_buffer_handle);
            SetError(result);
        }
    }

    void WaitRendering(uint64_t buffer_id) override
    {
        WaitRenderingOp op;
        op.buffer_id = buffer_id;
        magma_status_t result = channel_write(&op, sizeof(op), nullptr, 0);
        if (result != MAGMA_STATUS_OK) {
            SetError(result);
            return;
        }
        magma_status_t error;
        result = WaitError(&error);
        if (result != 0) {
            SetError(result);
            return;
        }

        if (error != 0)
            SetError(error);
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
            error_ = DRET_MSG(error, "ZirconPlatformIpcConnection encountered async error");
    }

    magma_status_t WaitError(magma_status_t* error_out)
    {
        return WaitMessage(reinterpret_cast<uint8_t*>(error_out), sizeof(*error_out), true);
    }

    magma_status_t WaitMessage(uint8_t* msg_out, uint32_t msg_size, bool blocking)
    {
        zx_signals_t signals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
        zx_signals_t pending = 0;

        zx_status_t status = channel_.wait_one(signals, blocking ? zx::time::infinite() : zx::time(), &pending);
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
        return fdio_handle_fd(notification_channel_.get(), ZX_CHANNEL_READABLE, 0, true);
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
    uint32_t next_context_id_{};
    magma_status_t error_{};
};

std::unique_ptr<PlatformIpcConnection>
PlatformIpcConnection::Create(uint32_t device_handle, uint32_t device_notification_handle)
{
    return std::unique_ptr<ZirconPlatformIpcConnection>(new ZirconPlatformIpcConnection(
        zx::channel(device_handle), zx::channel(device_notification_handle)));
}

static magma_status_t channel_send_callback(msd_channel_t channel, void* data, uint64_t size)
{
    zx_status_t status = zx_channel_write(channel, 0, data, size, nullptr, 0);
    if (status != ZX_OK)
        return DRETF(MAGMA_STATUS_INTERNAL_ERROR, "Failed writing to channel %d", status);
    return MAGMA_STATUS_OK;
}

std::shared_ptr<PlatformConnection>
PlatformConnection::Create(std::unique_ptr<PlatformConnection::Delegate> delegate)
{
    if (!delegate)
        return DRETP(nullptr, "attempting to create PlatformConnection with null delegate");

    zx::channel local_endpoint;
    zx::channel remote_endpoint;
    auto status = zx::channel::create(0, &local_endpoint, &remote_endpoint);
    if (status != ZX_OK)
        return DRETP(nullptr, "zx::channel::create failed");

    zx::channel local_notification_endpoint;
    zx::channel remote_notification_endpoint;
    status = zx::channel::create(0, &local_notification_endpoint, &remote_notification_endpoint);
    if (status != ZX_OK)
        return DRETP(nullptr, "zx::channel::create failed");
    delegate->SetNotificationChannel(&channel_send_callback, local_notification_endpoint.get());

    auto shutdown_event = magma::PlatformEvent::Create();
    DASSERT(shutdown_event);

    return std::shared_ptr<ZirconPlatformConnection>(new ZirconPlatformConnection(
        std::move(delegate), std::move(local_endpoint), std::move(remote_endpoint),
        std::move(local_notification_endpoint), std::move(remote_notification_endpoint),
        std::move(shutdown_event)));
}

} // namespace magma
