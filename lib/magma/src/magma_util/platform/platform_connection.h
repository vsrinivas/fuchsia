// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_CONNECTION_H_
#define GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_CONNECTION_H_

#include "magma.h"
#include "magma_util/macros.h"
#include "magma_util/status.h"
#include "msd_defs.h"
#include "platform_buffer.h"
#include "platform_event.h"
#include "platform_object.h"
#include "platform_semaphore.h"
#include "platform_thread.h"

#include <memory>

namespace magma {

// Any implementation of PlatformIpcConnection shall be threadsafe.
class PlatformIpcConnection : public magma_connection_t {
public:
    virtual ~PlatformIpcConnection() {}

    static std::unique_ptr<PlatformIpcConnection> Create(uint32_t device_handle,
                                                         uint32_t device_return_handle);

    // Imports a buffer for use in the system driver
    virtual magma_status_t ImportBuffer(PlatformBuffer* buffer) = 0;
    // Destroys the buffer with |buffer_id| within this connection
    // returns false if |buffer_id| has not been imported
    virtual magma_status_t ReleaseBuffer(uint64_t buffer_id) = 0;

    // Imports an object for use in the system driver
    virtual magma_status_t ImportObject(uint32_t handle, PlatformObject::Type object_type) = 0;

    // Releases the connection's reference to the given object.
    virtual magma_status_t ReleaseObject(uint64_t object_id, PlatformObject::Type object_type) = 0;

    // Creates a context and returns the context id
    virtual void CreateContext(uint32_t* context_id_out) = 0;
    // Destroys a context for the given id
    virtual void DestroyContext(uint32_t context_id) = 0;

    virtual magma_status_t GetError() = 0;

    virtual void ExecuteCommandBuffer(uint32_t command_buffer_handle, uint32_t context_id) = 0;

    virtual magma_status_t MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va, uint64_t page_offset,
                                        uint64_t page_count, uint64_t flags) = 0;

    virtual magma_status_t UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va) = 0;

    virtual magma_status_t CommitBuffer(uint64_t buffer_id, uint64_t page_offset,
                                        uint64_t page_count) = 0;

    virtual int GetNotificationChannelFd() = 0;
    virtual magma_status_t ReadNotificationChannel(void* buffer, size_t buffer_size,
                                                   size_t* buffer_size_out) = 0;
    virtual void ExecuteImmediateCommands(uint32_t context_id, uint64_t command_count,
                                          magma_system_inline_command_buffer* command_buffers) = 0;

    static PlatformIpcConnection* cast(magma_connection_t* connection)
    {
        DASSERT(connection);
        DASSERT(connection->magic_ == kMagic);
        return static_cast<PlatformIpcConnection*>(connection);
    }

protected:
    PlatformIpcConnection() { magic_ = kMagic; }

private:
    static const uint32_t kMagic = 0x636f6e6e; // "conn" (Connection)
};

class PlatformConnection {
public:
    class Delegate {
    public:
        virtual ~Delegate() {}
        virtual bool ImportBuffer(uint32_t handle, uint64_t* buffer_id_out) = 0;
        virtual bool ReleaseBuffer(uint64_t buffer_id) = 0;

        virtual bool ImportObject(uint32_t handle, PlatformObject::Type object_type) = 0;
        virtual bool ReleaseObject(uint64_t object_id, PlatformObject::Type object_type) = 0;

        virtual bool CreateContext(uint32_t context_id) = 0;
        virtual bool DestroyContext(uint32_t context_id) = 0;

        virtual magma::Status ExecuteCommandBuffer(uint32_t command_buffer_handle,
                                                   uint32_t context_id) = 0;

        virtual bool MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va, uint64_t page_offset,
                                  uint64_t page_count, uint64_t flags) = 0;
        virtual bool UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va) = 0;
        virtual bool CommitBuffer(uint64_t buffer_id, uint64_t page_offset,
                                  uint64_t page_count) = 0;
        virtual void SetNotificationCallback(msd_connection_notification_callback_t callback,
                                             void* token) = 0;
        virtual magma::Status ExecuteImmediateCommands(uint32_t context_id, uint64_t commands_size,
                                                       void* commands, uint64_t semaphore_count,
                                                       uint64_t* semaphore_ids) = 0;
    };

    PlatformConnection(std::shared_ptr<magma::PlatformEvent> shutdown_event)
        : shutdown_event_(std::move(shutdown_event))
    {
    }

    virtual ~PlatformConnection() {}

    static std::shared_ptr<PlatformConnection> Create(std::unique_ptr<Delegate> Delegate);
    virtual uint32_t GetHandle() = 0;
    // This handle is used to asynchronously return information to the client.
    virtual uint32_t GetNotificationChannel() = 0;

    // handles a single request, returns false if anything has put it into an illegal state
    // or if the remote has closed
    virtual bool HandleRequest() = 0;

    std::shared_ptr<magma::PlatformEvent> ShutdownEvent() { return shutdown_event_; }

    static void RunLoop(std::shared_ptr<magma::PlatformConnection> connection)
    {
        magma::PlatformThreadHelper::SetCurrentThreadName("ConnectionThread");
        while (connection->HandleRequest())
            ;
        // the runloop terminates when the remote closes, or an error is experienced
        // so this is the apropriate time to let the connection go out of scope and be destroyed
    }

private:
    std::shared_ptr<magma::PlatformEvent> shutdown_event_;
};

} // namespace magma

#endif // GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_CONNECTION_H_
