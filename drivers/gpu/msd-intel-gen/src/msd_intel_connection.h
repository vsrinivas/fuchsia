// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_CONNECTION_H
#define MSD_INTEL_CONNECTION_H

#include "command_buffer.h"
#include "engine_command_streamer.h"
#include "magma_util/macros.h"
#include "msd.h"
#include <memory>

class ClientContext;

class MsdIntelConnection {
public:
    class Owner {
    public:
        virtual magma::Status SubmitCommandBuffer(std::unique_ptr<CommandBuffer> cmd_buf) = 0;
        virtual void DestroyContext(std::shared_ptr<ClientContext> client_context) = 0;
        virtual std::shared_ptr<GpuMappingCache> mapping_cache() = 0;
    };

    static std::unique_ptr<MsdIntelConnection>
    Create(Owner* owner, std::shared_ptr<magma::PlatformBuffer> scratch_buffer);

    virtual ~MsdIntelConnection() {}

    std::shared_ptr<PerProcessGtt> per_process_gtt() { return ppgtt_; }

    magma::Status SubmitCommandBuffer(std::unique_ptr<CommandBuffer> cmd_buf)
    {
        return owner_->SubmitCommandBuffer(std::move(cmd_buf));
    }

    void DestroyContext(std::shared_ptr<ClientContext> client_context)
    {
        return owner_->DestroyContext(std::move(client_context));
    }

    bool context_killed() { return context_killed_; }

    void set_context_killed() { context_killed_ = true; }

private:
    MsdIntelConnection(Owner* owner, std::shared_ptr<PerProcessGtt> ppgtt)
        : owner_(owner), ppgtt_(std::move(ppgtt))
    {
    }

    static const uint32_t kMagic = 0x636f6e6e; // "conn" (Connection)

    Owner* owner_;
    std::shared_ptr<PerProcessGtt> ppgtt_;
    bool context_killed_ = false;
};

class MsdIntelAbiConnection : public msd_connection_t {
public:
    MsdIntelAbiConnection(std::shared_ptr<MsdIntelConnection> ptr) : ptr_(std::move(ptr))
    {
        magic_ = kMagic;
    }

    static MsdIntelAbiConnection* cast(msd_connection_t* connection)
    {
        DASSERT(connection);
        DASSERT(connection->magic_ == kMagic);
        return static_cast<MsdIntelAbiConnection*>(connection);
    }

    std::shared_ptr<MsdIntelConnection> ptr() { return ptr_; }

private:
    std::shared_ptr<MsdIntelConnection> ptr_;
    static const uint32_t kMagic = 0x636f6e6e; // "conn" (Connection)
};

#endif // MSD_INTEL_CONNECTION_H
