// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _PLATFORM_CONNECTION_H_
#define _PLATFORM_CONNECTION_H_

#include "magma_system.h"
#include "magma_util/macros.h"
#include "platform_buffer.h"

#include <memory>

namespace magma {

class PlatformIpcConnection : public magma_system_connection {
public:
    virtual ~PlatformIpcConnection() {}

    static std::unique_ptr<PlatformIpcConnection> Create(uint32_t device_handle);

    // Imports a buffer for use in the system driver
    virtual bool ImportBuffer(std::unique_ptr<PlatformBuffer> buffer) = 0;
    // Destroys the buffer with |id| within this connection
    // returns false if |id| has not been imported
    virtual bool ReleaseBuffer(uint64_t id) = 0;
    // Returns the PlatformBuffer for |id|
    // Returns nullptr if |id| is invalid
    virtual PlatformBuffer* LookupBuffer(uint64_t id) = 0;

    // Creates a context and returns the context id
    virtual void CreateContext(uint32_t* context_id_out) = 0;
    // Destroys a context for the given id
    virtual void DestroyContext(uint32_t context_id) = 0;

    virtual int32_t GetError() = 0;

    virtual void ExecuteCommandBuffer(magma_system_command_buffer* command_buffer,
                                      uint32_t context_id) = 0;

    // Blocks until all gpu work currently queued that references the buffer
    // with |id| has completed.
    virtual void WaitRendering(uint64_t buffer_id) = 0;

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
        virtual bool ImportBuffer(uint32_t handle, uint64_t* id_out) = 0;
        virtual bool ReleaseBuffer(uint64_t id) = 0;

        virtual bool CreateContext(uint32_t context_id) = 0;
        virtual bool DestroyContext(uint32_t context_id) = 0;
        virtual bool ExecuteCommandBuffer(magma_system_command_buffer* command_buffer,
                                          uint32_t context_id) = 0;

        virtual bool WaitRendering(uint64_t id) = 0;
    };

    static std::unique_ptr<PlatformConnection> Create(std::unique_ptr<Delegate> Delegate);
    virtual uint32_t GetHandle() = 0;
};

} // namespace magma

#endif //_PLATFORM_CONNECTION_H_