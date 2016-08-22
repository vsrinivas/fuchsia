// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_CONTEXT_H
#define MSD_INTEL_CONTEXT_H

#include "msd.h"
#include "msd_intel_buffer.h"
#include "ringbuffer.h"
#include "types.h"
#include <map>
#include <memory>

class MsdIntelContext : public msd_context {
public:
    MsdIntelContext();

    void SetEngineState(EngineCommandStreamerId id, std::unique_ptr<MsdIntelBuffer> context_buffer,
                        std::unique_ptr<Ringbuffer> ringbuffer);

    bool MapGpu(AddressSpace* address_space, EngineCommandStreamerId id);
    bool UnmapGpu(AddressSpace* address_space, EngineCommandStreamerId id);

    // Gets the gpu address of the context buffer if pinned.
    bool GetGpuAddress(EngineCommandStreamerId id, gpu_addr_t* addr_out);

    static MsdIntelContext* cast(msd_context* context)
    {
        DASSERT(context);
        DASSERT(context->magic_ == kMagic);
        return static_cast<MsdIntelContext*>(context);
    }

private:
    MsdIntelBuffer* get_buffer(EngineCommandStreamerId id)
    {
        auto iter = state_map_.find(id);
        return iter == state_map_.end() ? nullptr : iter->second.context_buffer.get();
    }

    Ringbuffer* get_ringbuffer(EngineCommandStreamerId id)
    {
        auto iter = state_map_.find(id);
        return iter == state_map_.end() ? nullptr : iter->second.ringbuffer.get();
    }

    struct PerEngineState {
        std::unique_ptr<MsdIntelBuffer> context_buffer;
        std::unique_ptr<Ringbuffer> ringbuffer;
        int32_t pinned_address_space_id;
    };

    std::map<EngineCommandStreamerId, PerEngineState> state_map_;

    static const uint32_t kMagic = 0x63747874; // "ctxt"
    static constexpr int32_t kNotPinned = -1;

    friend class TestContext;
};

#endif // MSD_INTEL_CONTEXT_H
