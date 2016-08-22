// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_context.h"

MsdIntelContext::MsdIntelContext() { magic_ = kMagic; }

void MsdIntelContext::SetEngineState(EngineCommandStreamerId id,
                                     std::unique_ptr<MsdIntelBuffer> context_buffer,
                                     std::unique_ptr<Ringbuffer> ringbuffer)
{
    DASSERT(context_buffer);
    DASSERT(ringbuffer);

    auto iter = state_map_.find(id);
    DASSERT(iter == state_map_.end());

    state_map_[id] = PerEngineState{std::move(context_buffer), std::move(ringbuffer), kNotPinned};
}

bool MsdIntelContext::MapGpu(AddressSpace* address_space, EngineCommandStreamerId id)
{
    auto iter = state_map_.find(id);
    if (iter == state_map_.end())
        return DRETF(false, "couldn't find engine command streamer");

    DLOG("Pinning context for engine %d", id);

    PerEngineState& state = iter->second;

    if (state.pinned_address_space_id == address_space->id())
        return true;

    if (!state.context_buffer->MapGpu(address_space, PAGE_SIZE))
        return DRETF(false, "context pin failed");

    if (!state.ringbuffer->Map(address_space)) {
        state.context_buffer->UnmapGpu(address_space);
        return DRETF(false, "ringbuffer pin failed");
    }

    state.pinned_address_space_id = address_space->id();

    return true;
}

bool MsdIntelContext::UnmapGpu(AddressSpace* address_space, EngineCommandStreamerId id)
{
    auto iter = state_map_.find(id);
    if (iter == state_map_.end())
        return DRETF(false, "couldn't find engine command streamer");

    DLOG("Unpinning context for engine %d", id);

    PerEngineState& state = iter->second;

    if (state.pinned_address_space_id != address_space->id())
        return DRETF(false, "context not pinned to given address_space");

    bool ret = true;
    if (!state.context_buffer->UnmapGpu(address_space)) {
        DLOG("context unpin failed");
        ret = false;
    }

    if (!state.ringbuffer->Unmap(address_space)) {
        DLOG("ringbuffer unpin failed");
        ret = false;
    }

    state.pinned_address_space_id = kNotPinned;

    return DRETF(ret, "error while unpinning");
}

bool MsdIntelContext::GetGpuAddress(EngineCommandStreamerId id, gpu_addr_t* addr_out)
{
    auto iter = state_map_.find(id);
    if (iter == state_map_.end())
        return DRETF(false, "couldn't find engine command streamer");

    PerEngineState& state = iter->second;
    if (state.pinned_address_space_id == kNotPinned)
        return DRETF(false, "context not pinned");

    if (!state.context_buffer->GetGpuAddress(
            static_cast<AddressSpaceId>(state.pinned_address_space_id), addr_out))
        return DRETF(false, "failed to get gpu address");

    return true;
}

//////////////////////////////////////////////////////////////////////////////

void msd_context_destroy(msd_context* ctx) { delete MsdIntelContext::cast(ctx); }
