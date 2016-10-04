// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_CONNECTION_H
#define MSD_INTEL_CONNECTION_H

#include "command_buffer.h"
#include "engine_command_streamer.h"
#include "magma_util/macros.h"
#include "msd.h"
#include "msd_intel_context.h"
#include <memory>

class MsdIntelConnection : public ClientContext::Owner {
public:
    class Owner {
    public:
        virtual HardwareStatusPage* hardware_status_page(EngineCommandStreamerId id) = 0;
        // TODO(MA-71) have the connection own its own PPGTT address space so we dont need this
        virtual AddressSpace* gtt() = 0;
        virtual bool ExecuteCommandBuffer(std::unique_ptr<CommandBuffer> cmd_buf) = 0;
        virtual bool WaitRendering(std::shared_ptr<MsdIntelBuffer> buf) = 0;
    };

    MsdIntelConnection(Owner* owner) : owner_(owner) {}

    virtual ~MsdIntelConnection() {}

    std::unique_ptr<ClientContext> CreateContext()
    {
        // Backing store creation deferred until context is used.
        return std::unique_ptr<ClientContext>(new ClientContext(this));
    }

    bool WaitRendering(std::shared_ptr<MsdIntelBuffer> buf)
    {
        return owner_->WaitRendering(std::move(buf));
    }

private:
    // ClientContext::Owner
    HardwareStatusPage* hardware_status_page(EngineCommandStreamerId id) override
    {
        return owner_->hardware_status_page(id);
    }

    AddressSpace* exec_address_space() override { return owner_->gtt(); }

    bool ExecuteCommandBuffer(std::unique_ptr<CommandBuffer> cmd_buf) override
    {
        return owner_->ExecuteCommandBuffer(std::move(cmd_buf));
    }

    static const uint32_t kMagic = 0x636f6e6e; // "conn" (Connection)

    Owner* owner_;
};

class MsdIntelAbiConnection : public msd_connection {
public:
    MsdIntelAbiConnection(std::shared_ptr<MsdIntelConnection> ptr) : ptr_(std::move(ptr))
    {
        magic_ = kMagic;
    }

    static MsdIntelAbiConnection* cast(msd_connection* connection)
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
