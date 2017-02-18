// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _PLATFORM_CONNECTION_H_
#define _PLATFORM_CONNECTION_H_

#include "magma_system.h"
#include "magma_util/macros.h"
#include "magma_util/status.h"
#include "platform_buffer.h"
#include "platform_event.h"

#include <memory>

namespace magma {

// Any implementation of PlatformIpcConnection shall be threadsafe.
class PlatformIpcConnection : public magma_system_connection {
public:
    virtual ~PlatformIpcConnection() {}

    static std::unique_ptr<PlatformIpcConnection> Create(uint32_t device_handle);

    // Imports a buffer for use in the system driver
    virtual magma_status_t ImportBuffer(PlatformBuffer* buffer) = 0;
    // Destroys the buffer with |buffer_id| within this connection
    // returns false if |buffer_id| has not been imported
    virtual magma_status_t ReleaseBuffer(uint64_t buffer_id) = 0;

    // Creates a context and returns the context id
    virtual void CreateContext(uint32_t* context_id_out) = 0;
    // Destroys a context for the given id
    virtual void DestroyContext(uint32_t context_id) = 0;

    virtual magma_status_t GetError() = 0;

    virtual void ExecuteCommandBuffer(uint64_t command_buffer_id, uint32_t context_id) = 0;

    // Blocks until all gpu work currently queued that references the buffer
    // with |buffer_id| has completed.
    virtual void WaitRendering(uint64_t buffer_id) = 0;

    virtual void PageFlip(uint64_t buffer_id, magma_system_pageflip_callback_t callback,
                          void* data) = 0;

    static PlatformIpcConnection* cast(magma_system_connection* connection)
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

        virtual bool CreateContext(uint32_t context_id) = 0;
        virtual bool DestroyContext(uint32_t context_id) = 0;

        virtual magma::Status ExecuteCommandBuffer(uint64_t command_buffer_id,
                                                   uint32_t context_id) = 0;
        virtual magma::Status WaitRendering(uint64_t buffer_id) = 0;

        virtual void PageFlip(uint64_t buffer_id, magma_system_pageflip_callback_t callback,
                              void* data) = 0;
    };

    PlatformConnection(std::unique_ptr<magma::PlatformEvent> shutdown_event)
        : shutdown_event_(std::move(shutdown_event))
    {
    }

    virtual ~PlatformConnection() {}

    static std::shared_ptr<PlatformConnection> Create(std::unique_ptr<Delegate> Delegate);
    virtual uint32_t GetHandle() = 0;

    // handles a single request, returns false if anything has put it into an illegal state
    // or if the remote has closed
    virtual bool HandleRequest() = 0;

    std::shared_ptr<magma::PlatformEvent> ShutdownEvent() { return shutdown_event_; }

    static void RunLoop(std::shared_ptr<magma::PlatformConnection> connection)
    {
        while (connection->HandleRequest())
            ;
        // the runloop terminates when the remote closes, or an error is experienced
        // so this is the apropriate time to let the connection go out of scope and be destroyed
    }

private:
    std::shared_ptr<magma::PlatformEvent> shutdown_event_;
};

} // namespace magma

#endif //_PLATFORM_CONNECTION_H_