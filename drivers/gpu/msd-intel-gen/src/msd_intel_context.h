// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_CONTEXT_H
#define MSD_INTEL_CONTEXT_H

#include "command_buffer.h"
#include "magma_util/status.h"
#include "msd.h"
#include "msd_intel_buffer.h"
#include "ppgtt.h"
#include "ringbuffer.h"
#include "types.h"
#include <map>
#include <memory>
#include <queue>

class MsdIntelConnection;

// Abstract base context.
class MsdIntelContext {
public:
    MsdIntelContext(std::shared_ptr<AddressSpace> address_space) : address_space_(address_space)
    {
        DASSERT(address_space_);
    }

    virtual ~MsdIntelContext() {}

    void SetEngineState(EngineCommandStreamerId id, std::unique_ptr<MsdIntelBuffer> context_buffer,
                        std::unique_ptr<Ringbuffer> ringbuffer);

    virtual bool Map(std::shared_ptr<AddressSpace> address_space, EngineCommandStreamerId id);
    virtual bool Unmap(EngineCommandStreamerId id);

    virtual std::weak_ptr<MsdIntelConnection> connection()
    {
        return std::weak_ptr<MsdIntelConnection>();
    }

    // Gets the gpu address of the context buffer if mapped.
    bool GetGpuAddress(EngineCommandStreamerId id, gpu_addr_t* addr_out);
    bool GetRingbufferGpuAddress(EngineCommandStreamerId id, gpu_addr_t* addr_out);

    MsdIntelBuffer* get_context_buffer(EngineCommandStreamerId id)
    {
        auto iter = state_map_.find(id);
        return iter == state_map_.end() ? nullptr : iter->second.context_buffer.get();
    }

    Ringbuffer* get_ringbuffer(EngineCommandStreamerId id)
    {
        auto iter = state_map_.find(id);
        return iter == state_map_.end() ? nullptr : iter->second.ringbuffer.get();
    }

    bool IsInitializedForEngine(EngineCommandStreamerId id)
    {
        return state_map_.find(id) != state_map_.end();
    }

    std::queue<std::unique_ptr<MappedBatch>>& pending_batch_queue() { return pending_batch_queue_; }

    std::shared_ptr<AddressSpace> exec_address_space() { return address_space_; }

private:
    struct PerEngineState {
        std::shared_ptr<MsdIntelBuffer> context_buffer;
        std::unique_ptr<GpuMapping> context_mapping;
        std::unique_ptr<Ringbuffer> ringbuffer;
    };

    std::map<EngineCommandStreamerId, PerEngineState> state_map_;
    std::queue<std::unique_ptr<MappedBatch>> pending_batch_queue_;
    std::shared_ptr<AddressSpace> address_space_;

    friend class TestContext;
};

class ClientContext : public MsdIntelContext {
public:
    ClientContext(std::weak_ptr<MsdIntelConnection> connection,
                  std::shared_ptr<AddressSpace> address_space)
        : MsdIntelContext(std::move(address_space)), connection_(connection)
    {
    }

    magma::Status SubmitCommandBuffer(std::unique_ptr<CommandBuffer> cmd_buf);

    std::weak_ptr<MsdIntelConnection> connection() override { return connection_; }

private:
    std::weak_ptr<MsdIntelConnection> connection_;
};

class MsdIntelAbiContext : public msd_context {
public:
    MsdIntelAbiContext(std::shared_ptr<ClientContext> ptr) : ptr_(std::move(ptr))
    {
        magic_ = kMagic;
    }

    static MsdIntelAbiContext* cast(msd_context* context)
    {
        DASSERT(context);
        DASSERT(context->magic_ == kMagic);
        return static_cast<MsdIntelAbiContext*>(context);
    }
    std::shared_ptr<ClientContext> ptr() { return ptr_; }

private:
    std::shared_ptr<ClientContext> ptr_;
    static const uint32_t kMagic = 0x63747874; // "ctxt"
};

#endif // MSD_INTEL_CONTEXT_H
