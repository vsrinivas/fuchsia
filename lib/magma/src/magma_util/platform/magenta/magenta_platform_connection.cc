// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magenta_platform_event.h"
#include "platform_connection.h"

#include "mx/channel.h"
#include <list>
#include <magenta/syscalls.h>
#include <magenta/types.h>

namespace magma {

enum OpCode {
    ImportBuffer,
    ReleaseBuffer,
    ImportObject,
    ReleaseObject,
    CreateContext,
    DestroyContext,
    ExecuteCommandBuffer,
    WaitRendering,
    PageFlip,
    GetError,
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

// Note PageFlipOp must be overlayed on a memory allocation dynamically sized
// for the number of semaphores.
struct PageFlipOp {
    const OpCode opcode = PageFlip;
    static constexpr uint32_t kNumHandles = 1;
    uint64_t buffer_id;
    uint64_t signal_semaphore_count;
    uint32_t wait_semaphore_count;
    uint64_t semaphore_ids[];

    static uint32_t size(uint32_t semaphore_count)
    {
        return sizeof(PageFlipOp) + sizeof(uint64_t) * semaphore_count;
    }

} __attribute__((packed));

struct GetErrorOp {
    const OpCode opcode = GetError;
    static constexpr uint32_t kNumHandles = 0;
} __attribute__((packed));

template <typename T>
T* OpCast(uint8_t* bytes, uint32_t num_bytes, mx_handle_t* handles, uint32_t kNumHandles)
{
    if (num_bytes != sizeof(T))
        return DRETP(nullptr, "wrong number of bytes in message, expected %zu, got %u", sizeof(T),
                     num_bytes);
    if (kNumHandles != T::kNumHandles)
        return DRETP(nullptr, "wrong number of handles in message");
    return reinterpret_cast<T*>(bytes);
}

template <>
PageFlipOp* OpCast<PageFlipOp>(uint8_t* bytes, uint32_t num_bytes, mx_handle_t* handles,
                               uint32_t kNumHandles)
{
    if (num_bytes < sizeof(PageFlipOp))
        return DRETP(nullptr, "too few bytes for a page flip: %u", num_bytes);

    auto page_flip_op = reinterpret_cast<PageFlipOp*>(bytes);
    const uint32_t expected_size =
        PageFlipOp::size(page_flip_op->wait_semaphore_count + page_flip_op->signal_semaphore_count);
    if (num_bytes != expected_size)
        return DRETP(nullptr, "wrong number of bytes in message, expected %u, got %u",
                     expected_size, num_bytes);
    if (kNumHandles != PageFlipOp::kNumHandles)
        return DRETP(nullptr, "wrong number of handles in message");
    return reinterpret_cast<PageFlipOp*>(bytes);
}

class MagentaPlatformConnection : public PlatformConnection,
                                  public std::enable_shared_from_this<MagentaPlatformConnection> {
public:
    MagentaPlatformConnection(std::unique_ptr<Delegate> delegate, mx::channel local_endpoint,
                              mx::channel remote_endpoint,
                              std::unique_ptr<magma::PlatformEvent> shutdown_event)
        : magma::PlatformConnection(std::move(shutdown_event)), delegate_(std::move(delegate)),
          local_endpoint_(std::move(local_endpoint)), remote_endpoint_(std::move(remote_endpoint))
    {
    }

    bool HandleRequest() override
    {
        constexpr uint32_t num_bytes = 256;
        constexpr uint32_t kNumHandles = 1;

        uint32_t actual_bytes;
        uint32_t actual_handles;

        uint8_t bytes[num_bytes];
        mx_handle_t handles[kNumHandles];

        auto shutdown_event = static_cast<MagentaPlatformEvent*>(ShutdownEvent().get());

        constexpr uint32_t kIndexChannel = 0;
        constexpr uint32_t kIndexShutdown = 1;

        constexpr uint32_t wait_item_count = 2;
        mx_wait_item_t wait_items[wait_item_count];
        wait_items[kIndexChannel] = {local_endpoint_.get(),
                                      MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED, 0};
        wait_items[kIndexShutdown] = {shutdown_event->mx_handle(), shutdown_event->mx_signal(), 0};

        if (mx_object_wait_many(wait_items, wait_item_count, MX_TIME_INFINITE) != MX_OK)
            return DRETF(false, "wait_many failed");

        if (wait_items[kIndexShutdown].pending & shutdown_event->mx_signal())
            return DRETF(false, "shutdown event signalled");

        if (wait_items[kIndexChannel].pending & MX_CHANNEL_PEER_CLOSED)
            return false; // No DRET because this happens on the normal connection closed path

        if (wait_items[kIndexChannel].pending & MX_CHANNEL_READABLE) {
            auto status = local_endpoint_.read(0, bytes, num_bytes, &actual_bytes, handles,
                                               kNumHandles, &actual_handles);
            if (status != MX_OK)
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
                case OpCode::PageFlip:
                    success = PageFlip(
                        OpCast<PageFlipOp>(bytes, actual_bytes, handles, actual_handles), handles);
                    break;
                case OpCode::GetError:
                    success =
                        GetError(OpCast<GetErrorOp>(bytes, actual_bytes, handles, actual_handles));
                    break;
                default:
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

private:
    bool ImportBuffer(ImportBufferOp* op, mx_handle_t* handle)
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

    bool ImportObject(ImportObjectOp* op, mx_handle_t* handle)
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

    bool ExecuteCommandBuffer(ExecuteCommandBufferOp* op, mx_handle_t* handle)
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

    bool PageFlip(PageFlipOp* op, mx_handle_t* handles)
    {
        DLOG("Operation: PageFlip");
        if (!op)
            return DRETF(false, "malformed message");

        auto buffer_presented_semaphore = magma::PlatformSemaphore::Import(handles[0]);
        if (!buffer_presented_semaphore)
            return DRETF(false, "couldn't import buffer_presented_semaphore from handle 0x%x",
                         handles[0]);

        magma::Status status =
            delegate_->PageFlip(op->buffer_id, op->wait_semaphore_count, op->signal_semaphore_count,
                                op->semaphore_ids, std::move(buffer_presented_semaphore));
        if (!status)
            SetError(status);
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

    void SetError(magma_status_t error)
    {
        if (!error_)
            error_ = DRET_MSG(error, "MagentaPlatformConnection encountered async error");
    }

    bool WriteError(magma_status_t error)
    {
        DLOG("Writing error %d to channel", error);
        auto status = local_endpoint_.write(0, &error, sizeof(error), nullptr, 0);
        return DRETF(status == MX_OK, "failed to write to channel");
    }

    std::unique_ptr<Delegate> delegate_;
    mx::channel local_endpoint_;
    mx::channel remote_endpoint_;
    magma_status_t error_{};
};

class MagentaPlatformIpcConnection : public PlatformIpcConnection {
public:
    MagentaPlatformIpcConnection(mx::channel channel) : channel_(std::move(channel)) {}

    // Imports a buffer for use in the system driver
    magma_status_t ImportBuffer(PlatformBuffer* buffer) override
    {
        if (!buffer)
            return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "attempting to import null buffer");

        uint32_t duplicate_handle;
        if (!buffer->duplicate_handle(&duplicate_handle))
            return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "failed to get duplicate_handle");

        mx_handle_t duplicate_handle_mx = duplicate_handle;

        ImportBufferOp op;
        magma_status_t result = channel_write(&op, sizeof(op), &duplicate_handle_mx, 1);
        if (result != MAGMA_STATUS_OK) {
            mx_handle_close(duplicate_handle);
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
        mx_handle_t duplicate_handle_mx = handle;

        ImportObjectOp op;
        op.object_type = object_type;

        magma_status_t result = channel_write(&op, sizeof(op), &duplicate_handle_mx, 1);
        if (result != MAGMA_STATUS_OK) {
            mx_handle_close(handle);
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

        mx_handle_t duplicate_handle_mx = command_buffer_handle;
        magma_status_t result = channel_write(&op, sizeof(op), &duplicate_handle_mx, 1);
        if (result != MAGMA_STATUS_OK) {
            mx_handle_close(command_buffer_handle);
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

    void PageFlip(uint64_t buffer_id, uint32_t wait_semaphore_count,
                  uint32_t signal_semaphore_count, const uint64_t* semaphore_ids,
                  uint32_t buffer_presented_handle) override
    {
        const uint32_t payload_size =
            PageFlipOp::size(wait_semaphore_count + signal_semaphore_count);
        std::unique_ptr<uint8_t[]> payload(new uint8_t[payload_size]);

        // placement new on top of the allocation
        auto op = new (payload.get()) PageFlipOp;
        op->buffer_id = buffer_id;
        op->signal_semaphore_count = signal_semaphore_count;
        op->wait_semaphore_count = wait_semaphore_count;
        for (uint32_t i = 0; i < wait_semaphore_count + signal_semaphore_count; i++) {
            op->semaphore_ids[i] = semaphore_ids[i];
        }

        mx_handle_t mx_buffer_presented_handle = buffer_presented_handle;
        magma_status_t result =
            channel_write(payload.get(), payload_size, &mx_buffer_presented_handle, 1);
        if (result != 0)
            SetError(result);
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

    void SetError(magma_status_t error)
    {
        if (!error_)
            error_ = DRET_MSG(error, "MagentaPlatformIpcConnection encountered async error");
    }

    magma_status_t WaitError(magma_status_t* error_out)
    {
        return WaitMessage(reinterpret_cast<uint8_t*>(error_out), sizeof(*error_out), true);
    }

    magma_status_t WaitMessage(uint8_t* msg_out, uint32_t msg_size, bool blocking)
    {
        mx_signals_t signals = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
        mx_signals_t pending = 0;

        mx_status_t status = channel_.wait_one(signals, blocking ? MX_TIME_INFINITE : 0, &pending);
        if (status == MX_ERR_TIMED_OUT) {
            DLOG("got MX_ERR_TIMED_OUT, returning true");
            return 0;
        } else if (status == MX_OK) {
            DLOG("got MX_OK, blocking: %s, readable: %s, closed %s", blocking ? "true" : "false",
                 pending & MX_CHANNEL_READABLE ? "true" : "false",
                 pending & MX_CHANNEL_PEER_CLOSED ? "true" : "false");
            if (pending & MX_CHANNEL_READABLE) {
                uint32_t actual_bytes;
                mx_status_t status =
                    channel_.read(0, msg_out, msg_size, &actual_bytes, nullptr, 0, nullptr);
                if (status != MX_OK)
                    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to read from channel");
                if (actual_bytes != msg_size)
                    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR,
                                    "read wrong number of bytes from channel");
            } else if (pending & MX_CHANNEL_PEER_CLOSED) {
                return DRET_MSG(MAGMA_STATUS_CONNECTION_LOST, "channel, closed");
            }
            return 0;
        } else {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to wait on channel");
        }
    }

private:
    magma_status_t channel_write(const void* bytes, uint32_t num_bytes, const mx_handle_t* handles,
                                 uint32_t num_handles)
    {
        mx_status_t status = channel_.write(0, bytes, num_bytes, handles, num_handles);
        switch (status) {
            case MX_OK:
                return MAGMA_STATUS_OK;
            case MX_ERR_PEER_CLOSED:
                return MAGMA_STATUS_CONNECTION_LOST;
            default:
                return MAGMA_STATUS_INTERNAL_ERROR;
        }
    }

    mx::channel channel_;
    uint32_t next_context_id_{};
    magma_status_t error_{};
};

std::unique_ptr<PlatformIpcConnection> PlatformIpcConnection::Create(uint32_t device_handle)
{
    return std::unique_ptr<MagentaPlatformIpcConnection>(
        new MagentaPlatformIpcConnection(mx::channel(device_handle)));
}

std::shared_ptr<PlatformConnection>
PlatformConnection::Create(std::unique_ptr<PlatformConnection::Delegate> delegate)
{
    if (!delegate)
        return DRETP(nullptr, "attempting to create PlatformConnection with null delegate");

    mx::channel local_endpoint;
    mx::channel remote_endpoint;
    auto status = mx::channel::create(0, &local_endpoint, &remote_endpoint);
    if (status != MX_OK)
        return DRETP(nullptr, "mx::channel::create failed");

    auto shutdown_event = magma::PlatformEvent::Create();
    DASSERT(shutdown_event);

    return std::shared_ptr<MagentaPlatformConnection>(
        new MagentaPlatformConnection(std::move(delegate), std::move(local_endpoint),
                                      std::move(remote_endpoint), std::move(shutdown_event)));
}

} // namespace magma
