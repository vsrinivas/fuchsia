// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magenta_platform_event.h"
#include "platform_connection.h"

#include "mx/channel.h"
#include "mx/waitset.h"
#include <magenta/types.h>
#include <map>

constexpr uint32_t kCookieChannel = 1;
constexpr uint32_t kCookieShutdown = 2;

namespace magma {

enum OpCode {
    ImportBuffer,
    ReleaseBuffer,
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
    static constexpr uint32_t kNumHandles = 0;
    uint64_t command_buffer_id;
    uint32_t context_id;
} __attribute__((packed));

struct WaitRenderingOp {
    const OpCode opcode = WaitRendering;
    static constexpr uint32_t kNumHandles = 0;
    uint64_t buffer_id;
} __attribute__((packed));

struct PageFlipOp {
    const OpCode opcode = PageFlip;
    static constexpr uint32_t kNumHandles = 0;
    uint64_t buffer_id;
} __attribute__((packed));

struct GetErrorOp {
    const OpCode opcode = GetError;
    static constexpr uint32_t kNumHandles = 0;
} __attribute__((packed));

template <typename T>
T* OpCast(uint8_t* bytes, uint32_t num_bytes, mx_handle_t* handles, uint32_t kNumHandles)
{
    if (num_bytes != sizeof(T))
        return DRETP(nullptr, "wrong number of bytes in message, expected %u, got %u", sizeof(T),
                     num_bytes);
    if (kNumHandles != T::kNumHandles)
        return DRETP(nullptr, "wrong number of handles in message");
    return reinterpret_cast<T*>(bytes);
}

struct PageFlipReply {
    uint64_t buffer_id;
    magma_status_t error;
} __attribute__((packed));

class MagentaPlatformConnection : public PlatformConnection {
public:
    MagentaPlatformConnection(std::unique_ptr<Delegate> delegate, mx::channel local_endpoint,
                              mx::channel remote_endpoint, mx::waitset waitset,
                              std::unique_ptr<magma::PlatformEvent> shutdown_event)
        : magma::PlatformConnection(std::move(shutdown_event)), delegate_(std::move(delegate)),
          local_endpoint_(std::move(local_endpoint)), remote_endpoint_(std::move(remote_endpoint)),
          waitset_(std::move(waitset))
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

        mx_signals_t pending = 0;

        uint32_t num_results = 2;
        mx_waitset_result_t results[num_results];

        if (waitset_.wait(MX_TIME_INFINITE, results, &num_results) != NO_ERROR)
            return DRETF(false, "waitset::wait failed");

        for (uint32_t i = 0; i < num_results; i++) {
            switch (results[i].cookie) {
            case kCookieChannel:
                if (results[i].status != NO_ERROR)
                    return DRETF(false, "error in waitset result: %d", results[i].status);
                pending = results[i].observed;
                break;

            case kCookieShutdown:
                DASSERT(results[i].status == NO_ERROR);
                DASSERT(results[i].observed);
                return false;
            }
        }

        if (pending & MX_CHANNEL_READABLE) {
            auto status = local_endpoint_.read(0, bytes, num_bytes, &actual_bytes, handles,
                                               kNumHandles, &actual_handles);
            if (status != NO_ERROR)
                return DRETF(false, "failed to read from channel");

            if (actual_bytes < sizeof(OpCode))
                return DRETF(false, "malformed message");

            OpCode* opcode = reinterpret_cast<OpCode*>(bytes);
            bool success = false;
            switch (*opcode) {
            case OpCode::ImportBuffer:
                success = ImportBuffer(
                    OpCast<ImportBufferOp>(bytes, actual_bytes, handles, actual_handles), handles);
                break;
            case OpCode::ReleaseBuffer:
                success = ReleaseBuffer(
                    OpCast<ReleaseBufferOp>(bytes, actual_bytes, handles, actual_handles));
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
                success = ExecuteCommandBuffer(
                    OpCast<ExecuteCommandBufferOp>(bytes, actual_bytes, handles, actual_handles));
                break;
            case OpCode::WaitRendering:
                success = WaitRendering(
                    OpCast<WaitRenderingOp>(bytes, actual_bytes, handles, actual_handles));
                break;
            case OpCode::PageFlip:
                success =
                    PageFlip(OpCast<PageFlipOp>(bytes, actual_bytes, handles, actual_handles));
                break;
            case OpCode::GetError:
                success =
                    GetError(OpCast<GetErrorOp>(bytes, actual_bytes, handles, actual_handles));
                break;
            default:
                break;
            }

            if (!success)
                return DRETF(false, "failed to interperet message");
        } else if (pending & MX_CHANNEL_PEER_CLOSED) {
            DLOG("remote endpoint closed");
            return false;
        }
        return true;
    }

    uint32_t GetHandle() override
    {
        DASSERT(remote_endpoint_);
        return remote_endpoint_.release();
    }

private:
    struct PageFlipData {
        uint64_t buffer_id;
        MagentaPlatformConnection* connection;
    };

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

    bool ExecuteCommandBuffer(ExecuteCommandBufferOp* op)
    {
        DLOG("Operation: ExecuteCommandBuffer");
        if (!op)
            return DRETF(false, "malformed message");
        if (!delegate_->ExecuteCommandBuffer(op->command_buffer_id, op->context_id))
            SetError(MAGMA_STATUS_INTERNAL_ERROR);
        return true;
    }

    bool WaitRendering(WaitRenderingOp* op)
    {
        DLOG("Operation: WaitRendering");
        if (!op)
            return DRETF(false, "malformed message");
        if (!delegate_->WaitRendering(op->buffer_id))
            SetError(MAGMA_STATUS_INTERNAL_ERROR);
        if (!WriteError(0))
            return false;
        return true;
    }

    bool PageFlip(PageFlipOp* op)
    {
        DLOG("Operation: PageFlip");
        if (!op)
            return DRETF(false, "malformed message");

        auto data = new PageFlipData();
        data->buffer_id = op->buffer_id;
        data->connection = this;

        delegate_->PageFlip(op->buffer_id, &PageFlipCallback, data);
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
        return DRETF(status == NO_ERROR, "failed to write to channel");
    }

    static void PageFlipCallback(magma_status_t error, void* data)
    {
        DLOG("MagentaPlatformConnection::PageFlipCallback");
        auto pageflip_data = reinterpret_cast<PageFlipData*>(data);
        auto connection = pageflip_data->connection;

        PageFlipReply reply;
        reply.buffer_id = pageflip_data->buffer_id;
        reply.error = error;

        auto status = connection->local_endpoint_.write(0, &reply, sizeof(reply), nullptr, 0);
        if (status != NO_ERROR)
            connection->SetError(DRET_MSG(status, "failed to write to channel"));

        delete pageflip_data;
    }

    std::unique_ptr<Delegate> delegate_;
    mx::channel local_endpoint_;
    mx::channel remote_endpoint_;
    mx::waitset waitset_;
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

    void ExecuteCommandBuffer(uint64_t command_buffer_id, uint32_t context_id) override
    {
        ExecuteCommandBufferOp op;
        op.command_buffer_id = command_buffer_id;
        op.context_id = context_id;
        magma_status_t result = channel_write(&op, sizeof(op), nullptr, 0);
        if (result != MAGMA_STATUS_OK)
            SetError(result);
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

    void PageFlip(uint64_t buffer_id, magma_system_pageflip_callback_t callback,
                  void* data) override
    {
        auto iter = pageflip_closure_map_.find(buffer_id);
        if (iter != pageflip_closure_map_.end()) {
            if (callback)
                callback(DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                                  "attempting to pageflip unavailable buffer"),
                         data);
            return;
        }

        pageflip_closure_map_[buffer_id] = {callback, data};

        PageFlipOp op;
        op.buffer_id = buffer_id;
        magma_status_t result = channel_write(&op, sizeof(op), nullptr, 0);
        if (result != 0) {
            if (callback)
                callback(DRET_MSG(result, "could not write to channel"), data);
            return;
        }

        // TODO(MA-118) wait for pageflip reply on a separate thread
        DLOG("waiting on reply");
        PageFlipReply reply;
        reply.buffer_id = 0;

        result = WaitMessage(reinterpret_cast<uint8_t*>(&reply), sizeof(reply), !first_frame);
        if (result != 0) {
            if (callback)
                callback(DRET_MSG(result, "failed to wait for pageflip response"), data);
            return;
        }

        if (reply.buffer_id) {
            auto iter = pageflip_closure_map_.find(reply.buffer_id);
            if (iter == pageflip_closure_map_.end()) {
                if (callback)
                    callback(
                        DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "no closure for buffer id in reply"),
                        data);
                return;
            }
            auto closure = iter->second;
            if (closure.callback)
                closure.callback(reply.error, closure.data);
            pageflip_closure_map_.erase(iter);
        }
        first_frame = false;
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
        if (status == ERR_TIMED_OUT) {
            DLOG("got ERR_TIMED_OUT, returning true");
            return 0;
        } else if (status == NO_ERROR) {
            DLOG("got NO_ERROR, blocking: %s, readable: %s, closed %s", blocking ? "true" : "false",
                 pending & MX_CHANNEL_READABLE ? "true" : "false",
                 pending & MX_CHANNEL_PEER_CLOSED ? "true" : "false");
            if (pending & MX_CHANNEL_READABLE) {
                uint32_t actual_bytes;
                mx_status_t status =
                    channel_.read(0, msg_out, msg_size, &actual_bytes, nullptr, 0, nullptr);
                if (status != NO_ERROR)
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
        case NO_ERROR:
            return MAGMA_STATUS_OK;
        case ERR_REMOTE_CLOSED:
            return MAGMA_STATUS_CONNECTION_LOST;
        default:
            return MAGMA_STATUS_INTERNAL_ERROR;
        }
    }

    struct pageflip_closure {
        magma_system_pageflip_callback_t callback;
        void* data;
    };

    mx::channel channel_;
    std::map<uint64_t, pageflip_closure> pageflip_closure_map_;
    uint32_t next_context_id_{};
    magma_status_t error_{};
    bool first_frame = true;
};

std::unique_ptr<PlatformIpcConnection> PlatformIpcConnection::Create(uint32_t device_handle)
{
    return std::unique_ptr<MagentaPlatformIpcConnection>(
        new MagentaPlatformIpcConnection(mx::channel(device_handle)));
}

std::unique_ptr<PlatformConnection>
PlatformConnection::Create(std::unique_ptr<PlatformConnection::Delegate> delegate)
{
    if (!delegate)
        return DRETP(nullptr, "attempting to create PlatformConnection with null delegate");

    mx::channel local_endpoint;
    mx::channel remote_endpoint;
    auto status = mx::channel::create(0, &local_endpoint, &remote_endpoint);
    if (status != NO_ERROR)
        return DRETP(nullptr, "mx::channel::create failed");

    mx::waitset waitset;
    status = mx::waitset::create(0, &waitset);
    if (status != NO_ERROR)
        return DRETP(nullptr, "mx::waitset::create failed");

    status = waitset.add(kCookieChannel, local_endpoint.get(),
                         MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED);
    if (status != NO_ERROR)
        return DRETP(nullptr, "mx::waitset::add failed");

    auto shutdown_event = magma::PlatformEvent::Create();
    DASSERT(shutdown_event);

    auto event = static_cast<MagentaPlatformEvent*>(shutdown_event.get());

    status = waitset.add(kCookieShutdown, event->mx_handle(), event->mx_signal());
    if (status != NO_ERROR)
        return DRETP(nullptr, "mx::waitset::add failed");

    return std::unique_ptr<MagentaPlatformConnection>(new MagentaPlatformConnection(
        std::move(delegate), std::move(local_endpoint), std::move(remote_endpoint),
        std::move(waitset), std::move(shutdown_event)));
}

} // namespace magma