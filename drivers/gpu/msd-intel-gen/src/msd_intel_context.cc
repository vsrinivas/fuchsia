// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_context.h"
#include "command_buffer.h"
#include <errno.h>

void MsdIntelContext::SetEngineState(EngineCommandStreamerId id,
                                     std::unique_ptr<MsdIntelBuffer> context_buffer,
                                     std::unique_ptr<Ringbuffer> ringbuffer)
{
    DASSERT(context_buffer);
    DASSERT(ringbuffer);

    auto iter = state_map_.find(id);
    DASSERT(iter == state_map_.end());

    state_map_[id] = PerEngineState{std::move(context_buffer), std::move(ringbuffer), kNotMapped};
}

bool MsdIntelContext::MapGpu(AddressSpace* address_space, EngineCommandStreamerId id)
{
    auto iter = state_map_.find(id);
    if (iter == state_map_.end())
        return DRETF(false, "couldn't find engine command streamer");

    DLOG("Mapping context for engine %d", id);

    PerEngineState& state = iter->second;

    if (state.mapped_address_space_id == address_space->id())
        return true;

    if (!state.context_buffer->MapGpu(address_space, PAGE_SIZE))
        return DRETF(false, "context map failed");

    if (!state.ringbuffer->Map(address_space)) {
        state.context_buffer->UnmapGpu(address_space);
        return DRETF(false, "ringbuffer map failed");
    }

    state.mapped_address_space_id = address_space->id();

    return true;
}

bool MsdIntelContext::UnmapGpu(AddressSpace* address_space, EngineCommandStreamerId id)
{
    auto iter = state_map_.find(id);
    if (iter == state_map_.end())
        return DRETF(false, "couldn't find engine command streamer");

    DLOG("Unmapping context for engine %d", id);

    PerEngineState& state = iter->second;

    if (state.mapped_address_space_id != address_space->id())
        return DRETF(false, "context not mapped to given address_space");

    bool ret = true;
    if (!state.context_buffer->UnmapGpu(address_space)) {
        DLOG("context unmap failed");
        ret = false;
    }

    if (!state.ringbuffer->Unmap(address_space)) {
        DLOG("ringbuffer unmap failed");
        ret = false;
    }

    state.mapped_address_space_id = kNotMapped;

    return DRETF(ret, "error while unmapping");
}

bool MsdIntelContext::GetGpuAddress(EngineCommandStreamerId id, gpu_addr_t* addr_out)
{
    auto iter = state_map_.find(id);
    if (iter == state_map_.end())
        return DRETF(false, "couldn't find engine command streamer");

    PerEngineState& state = iter->second;
    if (state.mapped_address_space_id == kNotMapped)
        return DRETF(false, "context not mapped");

    if (!state.context_buffer->GetGpuAddress(
            static_cast<AddressSpaceId>(state.mapped_address_space_id), addr_out))
        return DRETF(false, "failed to get gpu address");

    return true;
}

bool MsdIntelContext::GetRingbufferGpuAddress(EngineCommandStreamerId id, gpu_addr_t* addr_out)
{
    auto iter = state_map_.find(id);
    if (iter == state_map_.end())
        return DRETF(false, "couldn't find engine command streamer");

    PerEngineState& state = iter->second;
    if (state.mapped_address_space_id == kNotMapped)
        return DRETF(false, "context not mapped");

    if (!state.ringbuffer->GetGpuAddress(static_cast<AddressSpaceId>(state.mapped_address_space_id),
                                         addr_out))
        return DRETF(false, "failed to get gpu address");

    return true;
}

//////////////////////////////////////////////////////////////////////////////

void msd_context_destroy(msd_context* ctx) { delete MsdIntelAbiContext::cast(ctx); }

int32_t msd_context_execute_command_buffer(msd_context* ctx, magma_system_command_buffer* cmd_buf,
                                           msd_buffer** exec_resources)
{
    return 0;
}
