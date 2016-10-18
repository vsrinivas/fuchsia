// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/platform/platform_connection.h"

#include <errno.h>
#include <magenta/types.h>
#include <map>

namespace magma {

class MagentaPlatformConnection : public PlatformConnection {
public:
    MagentaPlatformConnection(std::unique_ptr<Delegate> delegate) : delegate_(std::move(delegate))
    {
        // Temporarily satisfy asserts in magma_system.cc until real ipc stuff is in
        handle_ = 0xdeadbeef;
    }

    virtual uint32_t GetHandle() { return handle_; }

    Delegate* delegate() { return delegate_.get(); }

private:
    std::unique_ptr<Delegate> delegate_;
    mx_handle_t handle_;
};

class MagentaPlatformIpcConnection : public PlatformIpcConnection {
public:
    MagentaPlatformIpcConnection(MagentaPlatformConnection* connection) : connection_(connection) {}

    // Imports a buffer for use in the system driver
    bool ImportBuffer(std::unique_ptr<PlatformBuffer> buffer) override
    {
        if (!buffer)
            return DRETF(false, "attempting to import null buffer");

        if (buffer_map_.find(buffer->id()) != buffer_map_.end())
            return DRETF(false, "attempting to import buffer twice");

        uint32_t duplicate_handle;
        if (!buffer->duplicate_handle(&duplicate_handle))
            return DRETF(false, "failed to get duplicate_handle");

        uint64_t id;
        connection_->delegate()->ImportBuffer(duplicate_handle, &id);
        DASSERT(id == buffer->id());

        buffer_map_.insert(std::make_pair(buffer->id(), std::move(buffer)));
        return true;
    }

    // Destroys the buffer with |id| within this connection
    // returns false if the buffer with |id| has not been imported
    bool ReleaseBuffer(uint64_t id) override
    {
        auto iter = buffer_map_.find(id);
        if (iter == buffer_map_.end())
            return DRETF(false, "attempting to release invalid buffer handle");

        if (!connection_->delegate()->ReleaseBuffer(iter->second->id()))
            error_ = -EINVAL;

        buffer_map_.erase(iter);

        return true;
    }

    // Returns the PlatformBuffer for |id|
    // Returns nullptr if |id| is invalid
    PlatformBuffer* LookupBuffer(uint64_t id) override
    {
        auto iter = buffer_map_.find(id);
        if (iter == buffer_map_.end())
            return DRETP(nullptr, "attempting to lookup invalid buffer handle");
        return iter->second.get();
    }

    // Creates a context and returns the context id
    void CreateContext(uint32_t* context_id_out) override
    {
        auto context_id = next_context_id_++;
        *context_id_out = context_id;
        if (!connection_->delegate()->CreateContext(context_id))
            error_ = -EINVAL;
    }

    // Destroys a context for the given id
    void DestroyContext(uint32_t context_id) override
    {
        if (!connection_->delegate()->DestroyContext(context_id))
            error_ = -EINVAL;
    }

    int32_t GetError() override
    {
        int32_t result = error_;
        error_ = 0;
        return result;
    }

    void ExecuteCommandBuffer(struct magma_system_command_buffer* command_buffer,
                              uint32_t context_id) override
    {
        if (!connection_->delegate()->ExecuteCommandBuffer(command_buffer, context_id)) {
            error_ = -EINVAL;
            return;
        }
    }

private:
    MagentaPlatformConnection* connection_;
    std::map<uint64_t, std::unique_ptr<PlatformBuffer>> buffer_map_;
    uint32_t next_context_id_{};
    int32_t error_{};
};

// Only to get stuff working until the actual ipc stuff goes in
MagentaPlatformConnection* g_connection;

std::unique_ptr<PlatformIpcConnection> PlatformIpcConnection::Create(uint32_t device_handle)
{
    DASSERT(device_handle == 0xdeadbeef);
    DASSERT(g_connection);
    return std::unique_ptr<MagentaPlatformIpcConnection>(
        new MagentaPlatformIpcConnection(g_connection));
}

std::unique_ptr<PlatformConnection>
PlatformConnection::Create(std::unique_ptr<PlatformConnection::Delegate> delegate)
{
    if (!delegate)
        return DRETP(nullptr, "attempting to create PlatformConnection with null delegate");
    auto result = std::unique_ptr<MagentaPlatformConnection>(
        new MagentaPlatformConnection(std::move(delegate)));
    g_connection = result.get();
    return result;
}

} // namespace magma