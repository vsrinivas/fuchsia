// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_CONTEXT_H
#define MSD_INTEL_CONTEXT_H

#include "hardware_status_page.h"
#include "msd.h"
#include "msd_intel_buffer.h"
#include "ringbuffer.h"
#include "types.h"
#include <map>
#include <memory>

// Abstract base context.
class MsdIntelContext : public msd_context {
public:
    MsdIntelContext();

    virtual ~MsdIntelContext() {}

    void SetEngineState(EngineCommandStreamerId id, std::unique_ptr<MsdIntelBuffer> context_buffer,
                        std::unique_ptr<Ringbuffer> ringbuffer);

    virtual bool Map(AddressSpace* address_space, EngineCommandStreamerId id)
    {
        return MapGpu(address_space, id);
    }

    virtual bool Unmap(AddressSpace* address_space, EngineCommandStreamerId id)
    {
        return UnmapGpu(address_space, id);
    }

    // Gets the gpu address of the context buffer if mapped.
    bool GetGpuAddress(EngineCommandStreamerId id, gpu_addr_t* addr_out);
    bool GetRingbufferGpuAddress(EngineCommandStreamerId id, gpu_addr_t* addr_out);

    virtual HardwareStatusPage* hardware_status_page(EngineCommandStreamerId id) = 0;

    static MsdIntelContext* cast(msd_context* context)
    {
        DASSERT(context);
        DASSERT(context->magic_ == kMagic);
        return static_cast<MsdIntelContext*>(context);
    }

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

protected:
    bool MapGpu(AddressSpace* address_space, EngineCommandStreamerId id);
    bool UnmapGpu(AddressSpace* address_space, EngineCommandStreamerId id);

private:
    struct PerEngineState {
        std::unique_ptr<MsdIntelBuffer> context_buffer;
        std::unique_ptr<Ringbuffer> ringbuffer;
        int32_t mapped_address_space_id;
    };

    std::map<EngineCommandStreamerId, PerEngineState> state_map_;

    static const uint32_t kMagic = 0x63747874; // "ctxt"
    static constexpr int32_t kNotMapped = -1;

    friend class TestContext;
};

class ClientContext : public MsdIntelContext {
public:
    class Owner {
    public:
        virtual HardwareStatusPage* hardware_status_page(EngineCommandStreamerId id) = 0;
    };

    ClientContext(Owner* owner) : owner_(owner) {}

    HardwareStatusPage* hardware_status_page(EngineCommandStreamerId id) override
    {
        return owner_->hardware_status_page(id);
    }

private:
    Owner* owner_;
};

#endif // MSD_INTEL_CONTEXT_H
