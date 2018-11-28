// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_connection.h"

#include "zircon_platform_event.h"
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/task.h>
#include <lib/async/time.h>
#include <lib/async/wait.h>
#include <lib/zx/channel.h>
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

class ZirconPlatformConnection : public PlatformConnection {
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

    ZirconPlatformConnection(std::unique_ptr<Delegate> delegate, zx::channel local_endpoint,
                             zx::channel remote_endpoint, zx::channel local_notification_endpoint,
                             zx::channel remote_notification_endpoint,
                             std::shared_ptr<magma::PlatformEvent> shutdown_event)
        : magma::PlatformConnection(shutdown_event), delegate_(std::move(delegate)),
          local_endpoint_(std::move(local_endpoint)), remote_endpoint_(std::move(remote_endpoint)),
          local_notification_endpoint_(std::move(local_notification_endpoint)),
          remote_notification_endpoint_(std::move(remote_notification_endpoint)),
          async_loop_(&kAsyncLoopConfigNoAttachToThread),
          async_wait_channel_(this, local_endpoint_.get(),
                              ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED),
          async_wait_shutdown_(
              this, static_cast<magma::ZirconPlatformEvent*>(shutdown_event.get())->zx_handle(),
              ZX_EVENT_SIGNALED)
    {
        delegate_->SetNotificationCallback(NotificationCallbackStatic, this);
    }

    ~ZirconPlatformConnection() { delegate_->SetNotificationCallback(nullptr, 0); }

    bool HandleRequest() override
    {
        zx_status_t status = async_loop_.Run(zx::time::infinite(), true); // Once
        if (status != ZX_OK) {
            DLOG("Run returned %d", status);
            return false;
        }
        return true;
    }

    bool BeginChannelWait()
    {
        zx_status_t status = async_begin_wait(async_loop()->dispatcher(), &async_wait_channel_);
        if (status != ZX_OK)
            return DRETF(false, "Couldn't begin wait on channel: %d", status);
        return true;
    }

    bool BeginShutdownWait()
    {
        zx_status_t status = async_begin_wait(async_loop()->dispatcher(), &async_wait_shutdown_);
        if (status != ZX_OK)
            return DRETF(false, "Couldn't begin wait on shutdown: %d", status);
        return true;
    }

    bool ReadChannel()
    {
        constexpr uint32_t num_bytes = kReceiveBufferSize;
        constexpr uint32_t kNumHandles = 1;

        uint32_t actual_bytes;
        uint32_t actual_handles;

        uint8_t bytes[num_bytes];
        zx_handle_t handles[kNumHandles];

        zx_status_t status = local_endpoint_.read(0, bytes, num_bytes, &actual_bytes, handles,
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
        } else if (wait == &async_wait_channel_ && signal->observed & ZX_CHANNEL_PEER_CLOSED) {
            quit = true;
        } else if (wait == &async_wait_channel_ && signal->observed & ZX_CHANNEL_READABLE) {
            if (!ReadChannel() || !BeginChannelWait()) {
                quit = true;
            }
        } else {
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
            DLOG("async_post_task failed, status %d", status);
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
                    local_notification_endpoint_.get(), 0, task->notification.u.channel_send.data,
                    task->notification.u.channel_send.size, nullptr, 0);
                if (status != ZX_OK)
                    return DRETF(MAGMA_STATUS_INTERNAL_ERROR, "Failed writing to channel %d",
                                 status);
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
            error_ = DRET_MSG(error, "ZirconPlatformConnection encountered dispatcher error");
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
    async::Loop async_loop_;
    AsyncWait async_wait_channel_;
    AsyncWait async_wait_shutdown_;
};

std::shared_ptr<PlatformConnection>
PlatformConnection::Create(std::unique_ptr<PlatformConnection::Delegate> delegate)
{
    if (!delegate)
        return DRETP(nullptr, "attempting to create PlatformConnection with null delegate");

    zx::channel local_endpoint;
    zx::channel remote_endpoint;
    zx_status_t status = zx::channel::create(0, &local_endpoint, &remote_endpoint);
    if (status != ZX_OK)
        return DRETP(nullptr, "zx::channel::create failed");

    zx::channel local_notification_endpoint;
    zx::channel remote_notification_endpoint;
    status = zx::channel::create(0, &local_notification_endpoint, &remote_notification_endpoint);
    if (status != ZX_OK)
        return DRETP(nullptr, "zx::channel::create failed");

    auto shutdown_event = magma::PlatformEvent::Create();
    if (!shutdown_event)
        return DRETP(nullptr, "Failed to create shutdown event");

    auto connection = std::make_shared<ZirconPlatformConnection>(
        std::move(delegate), std::move(local_endpoint), std::move(remote_endpoint),
        std::move(local_notification_endpoint), std::move(remote_notification_endpoint),
        std::shared_ptr<magma::PlatformEvent>(std::move(shutdown_event)));

    if (!connection->BeginChannelWait())
        return DRETP(nullptr, "Failed to begin channel wait");

    if (!connection->BeginShutdownWait())
        return DRETP(nullptr, "Failed to begin shutdown wait");

    return connection;
}

} // namespace magma
