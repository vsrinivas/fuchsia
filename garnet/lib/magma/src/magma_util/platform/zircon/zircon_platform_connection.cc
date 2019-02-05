// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_connection.h"

#include "platform_connection.h"
#include "zircon_platform_event.h"
#include <fuchsia/gpu/magma/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/task.h>
#include <lib/async/time.h>
#include <lib/async/wait.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

namespace magma {

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

class ZirconPlatformConnection : public PlatformConnection, public fuchsia::gpu::magma::Primary {
public:
    struct AsyncWait : public async_wait {
        AsyncWait(ZirconPlatformConnection* connection, zx_handle_t object, zx_signals_t trigger)
        {
            this->state = ASYNC_STATE_INIT;
            this->handler = AsyncWaitHandlerStatic;
            this->object = object;
            this->trigger = trigger;
            this->connection = connection;
        }
        ZirconPlatformConnection* connection;
    };

    struct AsyncTask : public async_task {
        AsyncTask(ZirconPlatformConnection* connection, msd_notification_t* notification)
        {
            this->state = ASYNC_STATE_INIT;
            this->handler = AsyncTaskHandlerStatic;
            this->deadline = async_now(connection->async_loop()->dispatcher());
            this->connection = connection;
            // Copy the notification struct
            this->notification = *notification;
        }

        ZirconPlatformConnection* connection;
        msd_notification_t notification;
    };

    ZirconPlatformConnection(std::unique_ptr<Delegate> delegate, zx::channel server_endpoint,
                             zx::channel client_endpoint, zx::channel server_notification_endpoint,
                             zx::channel client_notification_endpoint,
                             std::shared_ptr<magma::PlatformEvent> shutdown_event)
        : magma::PlatformConnection(shutdown_event), delegate_(std::move(delegate)),
#if !MAGMA_FIDL
          server_endpoint_(std::move(server_endpoint)),
#endif
          client_endpoint_(std::move(client_endpoint)),
          server_notification_endpoint_(std::move(server_notification_endpoint)),
          client_notification_endpoint_(std::move(client_notification_endpoint)),
          async_loop_(&kAsyncLoopConfigNoAttachToThread),
#if !MAGMA_FIDL
          async_wait_channel_(this, server_endpoint_.get(),
                              ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED),
#endif
          async_wait_shutdown_(
              this, static_cast<magma::ZirconPlatformEvent*>(shutdown_event.get())->zx_handle(),
              ZX_EVENT_SIGNALED)
#if MAGMA_FIDL
          ,
          binding_(this, std::move(server_endpoint), async_loop_.dispatcher())
#endif
    {
#if MAGMA_FIDL
        binding_.set_error_handler([this](zx_status_t status) { async_loop()->Quit(); });
#endif
        delegate_->SetNotificationCallback(NotificationCallbackStatic, this);
    }

    ~ZirconPlatformConnection() { delegate_->SetNotificationCallback(nullptr, 0); }

    bool HandleRequest() override
    {
        zx_status_t status = async_loop_.Run(zx::time::infinite(), true /* once */);
        if (status != ZX_OK)
            return false;
        return true;
    }

#if !MAGMA_FIDL
    bool BeginChannelWait()
    {
        zx_status_t status = async_begin_wait(async_loop()->dispatcher(), &async_wait_channel_);
        if (status != ZX_OK)
            return DRETF(false, "Couldn't begin wait on channel: %s", zx_status_get_string(status));
        return true;
    }
#endif

    bool BeginShutdownWait()
    {
        zx_status_t status = async_begin_wait(async_loop()->dispatcher(), &async_wait_shutdown_);
        if (status != ZX_OK)
            return DRETF(false, "Couldn't begin wait on shutdown: %s",
                         zx_status_get_string(status));
        return true;
    }

#if !MAGMA_FIDL
    bool ReadChannel()
    {
        constexpr uint32_t kNumBytes = fuchsia::gpu::magma::kReceiveBufferSize;
        constexpr uint32_t kNumHandles = 1;

        uint32_t actual_bytes;
        uint32_t actual_handles;

        uint8_t bytes[kNumBytes];
        zx_handle_t handles[kNumHandles];

        zx_status_t status = server_endpoint_.read(0, bytes, kNumBytes, &actual_bytes, handles,
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
                    OpCast<ImportBufferOp>(bytes, actual_bytes, handles, actual_handles), handles);
                break;
            case OpCode::ReleaseBuffer:
                success = ReleaseBuffer(
                    OpCast<ReleaseBufferOp>(bytes, actual_bytes, handles, actual_handles));
                break;
            case OpCode::ImportObject:
                success = ImportObject(
                    OpCast<ImportObjectOp>(bytes, actual_bytes, handles, actual_handles), handles);
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
                success = ExecuteCommandBuffer(
                    OpCast<ExecuteCommandBufferOp>(bytes, actual_bytes, handles, actual_handles),
                    handles);
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

        if (error_)
            return DRETF(false, "PlatformConnection encountered fatal error");

        return true;
    }
#endif

    uint32_t GetClientEndpoint() override
    {
        DASSERT(client_endpoint_);
        return client_endpoint_.release();
    }

    uint32_t GetClientNotificationEndpoint() override
    {
        DASSERT(client_notification_endpoint_);
        return client_notification_endpoint_.release();
    }

    async::Loop* async_loop() { return &async_loop_; }

private:
    static void AsyncWaitHandlerStatic(async_dispatcher_t* dispatcher, async_wait_t* async_wait,
                                       zx_status_t status, const zx_packet_signal_t* signal)
    {
        auto wait = static_cast<AsyncWait*>(async_wait);
        wait->connection->AsyncWaitHandler(dispatcher, wait, status, signal);
    }

    void AsyncWaitHandler(async_dispatcher_t* dispatcher, AsyncWait* wait, zx_status_t status,
                          const zx_packet_signal_t* signal)
    {
        if (status != ZX_OK)
            return;

        bool quit = false;
        if (wait == &async_wait_shutdown_) {
            DASSERT(signal->observed == ZX_EVENT_SIGNALED);
            quit = true;
            DLOG("got shutdown event");
        }
#if !MAGMA_FIDL
        else if (wait == &async_wait_channel_ && signal->observed & ZX_CHANNEL_PEER_CLOSED) {
            quit = true;
        } else if (wait == &async_wait_channel_ && signal->observed & ZX_CHANNEL_READABLE) {
            if (!ReadChannel() || !BeginChannelWait()) {
                quit = true;
            }
        }
#endif
        else {
            DASSERT(false);
        }

        if (quit) {
            async_loop()->Quit();
        }
    }

    // Could occur on an arbitrary thread (see |msd_connection_set_notification_callback|).
    // MSD must ensure we aren't in the process of destroying our connection.
    static void NotificationCallbackStatic(void* token, msd_notification_t* notification)
    {
        auto connection = static_cast<ZirconPlatformConnection*>(token);
        zx_status_t status = async_post_task(connection->async_loop()->dispatcher(),
                                             new AsyncTask(connection, notification));
        if (status != ZX_OK)
            DLOG("async_post_task failed, status %s", zx_status_get_string(status));
    }

    static void AsyncTaskHandlerStatic(async_dispatcher_t* dispatcher, async_task_t* async_task,
                                       zx_status_t status)
    {
        auto task = static_cast<AsyncTask*>(async_task);
        task->connection->AsyncTaskHandler(dispatcher, task, status);
        delete task;
    }

    bool AsyncTaskHandler(async_dispatcher_t* dispatcher, AsyncTask* task, zx_status_t status)
    {
        switch (static_cast<MSD_CONNECTION_NOTIFICATION_TYPE>(task->notification.type)) {
            case MSD_CONNECTION_NOTIFICATION_CHANNEL_SEND: {
                zx_status_t status = zx_channel_write(
                    server_notification_endpoint_.get(), 0, task->notification.u.channel_send.data,
                    task->notification.u.channel_send.size, nullptr, 0);
                if (status != ZX_OK)
                    return DRETF(false, "Failed writing to channel: %s", zx_status_get_string(status));
                return true;
            }
            case MSD_CONNECTION_NOTIFICATION_CONTEXT_KILLED:
                // Kill the connection.
                ShutdownEvent()->Signal();
                return true;
        }
        return DRETF(false, "Unhandled notification type: %d", task->notification.type);
    }

    bool ImportBuffer(ImportBufferOp* op, zx_handle_t* handle)
    {
        DLOG("ZirconPlatformConnection: ImportBuffer");
        if (!op)
            return DRETF(false, "malformed message");
        uint64_t buffer_id;
        if (!delegate_->ImportBuffer(*handle, &buffer_id))
            SetError(MAGMA_STATUS_INVALID_ARGS);
        return true;
    }

    void ImportBufferFIDL(zx::vmo vmo) override
    {
        DLOG("ZirconPlatformConnection - ImportBufferFIDL");
        uint64_t buffer_id;
        if (!delegate_->ImportBuffer(vmo.release(), &buffer_id)) {
            SetError(MAGMA_STATUS_INVALID_ARGS);
        }
    }

    bool ReleaseBuffer(ReleaseBufferOp* op)
    {
        DLOG("ZirconPlatformConnection: ReleaseBuffer");
        if (!op)
            return DRETF(false, "malformed message");
        if (!delegate_->ReleaseBuffer(op->buffer_id))
            SetError(MAGMA_STATUS_INVALID_ARGS);
        return true;
    }

    void ReleaseBufferFIDL(uint64_t buffer_id) override
    {
        DLOG("ZirconPlatformConnection: ReleaseBufferFIDL");
        if (!delegate_->ReleaseBuffer(buffer_id))
            SetError(MAGMA_STATUS_INVALID_ARGS);
    }

    bool ImportObject(ImportObjectOp* op, zx_handle_t* handle)
    {
        DLOG("ZirconPlatformConnection: ImportObject");
        if (!op)
            return DRETF(false, "malformed message");
        if (!delegate_->ImportObject(*handle, static_cast<PlatformObject::Type>(op->object_type)))
            SetError(MAGMA_STATUS_INVALID_ARGS);
        return true;
    }

    void ImportObjectFIDL(zx::handle handle, uint32_t object_type) override
    {
        DLOG("ZirconPlatformConnection: ImportObjectFIDL");
        if (!delegate_->ImportObject(handle.release(),
                                     static_cast<PlatformObject::Type>(object_type)))
            SetError(MAGMA_STATUS_INVALID_ARGS);
    }

    bool ReleaseObject(ReleaseObjectOp* op)
    {
        DLOG("ZirconPlatformConnection: ReleaseObject");
        if (!op)
            return DRETF(false, "malformed message");
        if (!delegate_->ReleaseObject(op->object_id,
                                      static_cast<PlatformObject::Type>(op->object_type)))
            SetError(MAGMA_STATUS_INVALID_ARGS);
        return true;
    }

    void ReleaseObjectFIDL(uint64_t object_id, uint32_t object_type) override
    {
        DLOG("ZirconPlatformConnection: ReleaseObjectFIDL");
        if (!delegate_->ReleaseObject(object_id, static_cast<PlatformObject::Type>(object_type)))
            SetError(MAGMA_STATUS_INVALID_ARGS);
    }

    bool CreateContext(CreateContextOp* op)
    {
        DLOG("ZirconPlatformConnection: CreateContext");
        if (!op)
            return DRETF(false, "malformed message");
        if (!delegate_->CreateContext(op->context_id))
            SetError(MAGMA_STATUS_INTERNAL_ERROR);
        return true;
    }

    void CreateContextFIDL(uint32_t context_id) override
    {
        DLOG("ZirconPlatformConnection: CreateContextFIDL");
        if (!delegate_->CreateContext(context_id))
            SetError(MAGMA_STATUS_INTERNAL_ERROR);
    }

    bool DestroyContext(DestroyContextOp* op)
    {
        DLOG("ZirconPlatformConnection: DestroyContext");
        if (!op)
            return DRETF(false, "malformed message");
        if (!delegate_->DestroyContext(op->context_id))
            SetError(MAGMA_STATUS_INTERNAL_ERROR);
        return true;
    }

    void DestroyContextFIDL(uint32_t context_id) override
    {
        DLOG("ZirconPlatformConnection: DestroyContextFIDL");
        if (!delegate_->DestroyContext(context_id))
            SetError(MAGMA_STATUS_INTERNAL_ERROR);
    }

    bool ExecuteCommandBuffer(ExecuteCommandBufferOp* op, zx_handle_t* handle)
    {
        DLOG("ZirconPlatformConnection: ExecuteCommandBuffer");
        if (!op)
            return DRETF(false, "malformed message");
        magma::Status status = delegate_->ExecuteCommandBuffer(*handle, op->context_id);
        if (status.get() == MAGMA_STATUS_CONTEXT_KILLED)
            ShutdownEvent()->Signal();
        if (!status)
            SetError(MAGMA_STATUS_INTERNAL_ERROR);
        return true;
    }

    void ExecuteCommandBufferFIDL(zx::handle command_buffer, uint32_t context_id) override
    {
        DLOG("ZirconPlatformConnection: ExecuteCommandBufferFIDL");
        magma::Status status =
            delegate_->ExecuteCommandBuffer(command_buffer.release(), context_id);
        if (status.get() == MAGMA_STATUS_CONTEXT_KILLED)
            ShutdownEvent()->Signal();
        if (!status)
            SetError(MAGMA_STATUS_INTERNAL_ERROR);
    }

    bool ExecuteImmediateCommands(ExecuteImmediateCommandsOp* op)
    {
        DLOG("ZirconPlatformConnection: ExecuteImmediateCommands");
        if (!op)
            return DRETF(false, "malformed message");

        magma::Status status = delegate_->ExecuteImmediateCommands(
            op->context_id, op->commands_size, op->command_data(), op->semaphore_count,
            op->semaphores);
        if (!status)
            SetError(status.get());
        return true;
    }

    void ExecuteImmediateCommandsFIDL(uint32_t context_id, ::std::vector<uint8_t> command_data_vec,
                                      ::fidl::VectorPtr<uint64_t> semaphores) override
    {
        DLOG("ZirconPlatformConnection: ExecuteImmediateCommandsFIDL");
        std::vector<uint64_t> semaphore_vec(semaphores.take());
        magma::Status status = delegate_->ExecuteImmediateCommands(
            context_id, command_data_vec.size(), command_data_vec.data(), semaphore_vec.size(),
            semaphore_vec.data());
        if (!status)
            SetError(status.get());
    }

    bool GetError(GetErrorOp* op)
    {
        DLOG("ZirconPlatformConnection: GetError");
        if (!op)
            return DRETF(false, "malformed message");
        magma_status_t result = error_;
        error_ = 0;
        if (!WriteError(result))
            return false;
        return true;
    }

    void GetErrorFIDL(fuchsia::gpu::magma::Primary::GetErrorFIDLCallback error_callback) override
    {
        magma_status_t result = error_;
        error_ = 0;
        error_callback(result);
    }

    bool MapBufferGpu(MapBufferGpuOp* op)
    {
        DLOG("ZirconPlatformConnection: MapBufferGpu");
        if (!op)
            return DRETF(false, "malformed message");
        if (!delegate_->MapBufferGpu(op->buffer_id, op->gpu_va, op->page_offset, op->page_count,
                                     op->flags))
            SetError(MAGMA_STATUS_INVALID_ARGS);
        return true;
    }

    void MapBufferGpuFIDL(uint64_t buffer_id, uint64_t gpu_va, uint64_t page_offset,
                          uint64_t page_count, uint64_t flags) override
    {
        DLOG("ZirconPlatformConnection: MapBufferGpuFIDL");
        if (!delegate_->MapBufferGpu(buffer_id, gpu_va, page_offset, page_count, flags))
            SetError(MAGMA_STATUS_INVALID_ARGS);
    }

    bool UnmapBufferGpu(UnmapBufferGpuOp* op)
    {
        DLOG("ZirconPlatformConnection: UnmapBufferGpu");
        if (!op)
            return DRETF(false, "malformed message");
        if (!delegate_->UnmapBufferGpu(op->buffer_id, op->gpu_va))
            SetError(MAGMA_STATUS_INVALID_ARGS);
        return true;
    }

    void UnmapBufferGpuFIDL(uint64_t buffer_id, uint64_t gpu_va) override
    {
        DLOG("ZirconPlatformConnection: UnmapBufferGpuFIDL");
        if (!delegate_->UnmapBufferGpu(buffer_id, gpu_va))
            SetError(MAGMA_STATUS_INVALID_ARGS);
    }

    bool CommitBuffer(CommitBufferOp* op)
    {
        DLOG("ZirconPlatformConnection: CommitBuffer");
        if (!op)
            return DRETF(false, "malformed message");
        if (!delegate_->CommitBuffer(op->buffer_id, op->page_offset, op->page_count))
            SetError(MAGMA_STATUS_INVALID_ARGS);
        return true;
    }

    void CommitBufferFIDL(uint64_t buffer_id, uint64_t page_offset, uint64_t page_count) override
    {
        DLOG("ZirconPlatformConnection: CommitBufferFIDL");
        if (!delegate_->CommitBuffer(buffer_id, page_offset, page_count))
            SetError(MAGMA_STATUS_INVALID_ARGS);
    }

    void SetError(magma_status_t error)
    {
        if (!error_)
            error_ = DRET_MSG(error, "ZirconPlatformConnection encountered dispatcher error");
    }

    bool WriteError(magma_status_t error)
    {
        DLOG("Writing error %d to channel", error);
        zx_status_t status = server_endpoint_.write(0, &error, sizeof(error), nullptr, 0);
        return DRETF(status == ZX_OK, "failed to write to channel");
    }

    std::unique_ptr<Delegate> delegate_;
    zx::channel server_endpoint_;
    zx::channel client_endpoint_;
    magma_status_t error_{};
    zx::channel server_notification_endpoint_;
    zx::channel client_notification_endpoint_;
    async::Loop async_loop_;
#if !MAGMA_FIDL
    AsyncWait async_wait_channel_;
#endif
    AsyncWait async_wait_shutdown_;

#if MAGMA_FIDL
    fidl::Binding<fuchsia::gpu::magma::Primary> binding_;
#endif
};

std::shared_ptr<PlatformConnection>
PlatformConnection::Create(std::unique_ptr<PlatformConnection::Delegate> delegate)
{
    if (!delegate)
        return DRETP(nullptr, "attempting to create PlatformConnection with null delegate");

    zx::channel server_endpoint;
    zx::channel client_endpoint;
    zx_status_t status = zx::channel::create(0, &server_endpoint, &client_endpoint);
    if (status != ZX_OK)
        return DRETP(nullptr, "zx::channel::create failed");

    zx::channel server_notification_endpoint;
    zx::channel client_notification_endpoint;
    status = zx::channel::create(0, &server_notification_endpoint, &client_notification_endpoint);
    if (status != ZX_OK)
        return DRETP(nullptr, "zx::channel::create failed");

    auto shutdown_event = magma::PlatformEvent::Create();
    if (!shutdown_event)
        return DRETP(nullptr, "Failed to create shutdown event");

    auto connection = std::make_shared<ZirconPlatformConnection>(
        std::move(delegate), std::move(server_endpoint), std::move(client_endpoint),
        std::move(server_notification_endpoint), std::move(client_notification_endpoint),
        std::shared_ptr<magma::PlatformEvent>(std::move(shutdown_event)));

#if !MAGMA_FIDL
    if (!connection->BeginChannelWait())
        return DRETP(nullptr, "Failed to begin channel wait");
#endif

    if (!connection->BeginShutdownWait())
        return DRETP(nullptr, "Failed to begin shutdown wait");

    return connection;
}

} // namespace magma
