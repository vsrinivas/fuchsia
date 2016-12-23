// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_context.h"
#include "address_space.h"
#include "command_buffer.h"
#include "msd_intel_connection.h"

void MsdIntelContext::SetEngineState(EngineCommandStreamerId id,
                                     std::unique_ptr<MsdIntelBuffer> context_buffer,
                                     std::unique_ptr<Ringbuffer> ringbuffer)
{
    DASSERT(context_buffer);
    DASSERT(ringbuffer);

    auto iter = state_map_.find(id);
    DASSERT(iter == state_map_.end());

    state_map_[id] = PerEngineState{std::move(context_buffer), nullptr, std::move(ringbuffer)};
}

bool MsdIntelContext::Map(std::shared_ptr<AddressSpace> address_space, EngineCommandStreamerId id)
{
    auto iter = state_map_.find(id);
    if (iter == state_map_.end())
        return DRETF(false, "couldn't find engine command streamer");

    DLOG("Mapping context for engine %d", id);

    PerEngineState& state = iter->second;

    if (state.context_mapping) {
        if (state.context_mapping->address_space_id() == address_space->id())
            return true;
        return DRETF(false, "already mapped to a different address space");
    }

    state.context_mapping =
        AddressSpace::MapBufferGpu(address_space, state.context_buffer, PAGE_SIZE);
    if (!state.context_mapping)
        return DRETF(false, "context map failed");

    if (!state.ringbuffer->Map(address_space)) {
        state.context_mapping.reset();
        return DRETF(false, "ringbuffer map failed");
    }

    return true;
}

bool MsdIntelContext::Unmap(AddressSpaceId address_space_id, EngineCommandStreamerId id)
{
    auto iter = state_map_.find(id);
    if (iter == state_map_.end())
        return DRETF(false, "couldn't find engine command streamer");

    DLOG("Unmapping context for engine %d", id);

    PerEngineState& state = iter->second;

    if (!state.context_mapping || state.context_mapping->address_space_id() != address_space_id)
        return DRETF(false, "context not mapped to given address_space");

    state.context_mapping.reset();

    if (!state.ringbuffer->Unmap())
        return DRETF(false, "ringbuffer unmap failed");

    return true;
}

bool MsdIntelContext::GetGpuAddress(EngineCommandStreamerId id, gpu_addr_t* addr_out)
{
    auto iter = state_map_.find(id);
    if (iter == state_map_.end())
        return DRETF(false, "couldn't find engine command streamer");

    PerEngineState& state = iter->second;
    if (!state.context_mapping)
        return DRETF(false, "context not mapped");

    *addr_out = state.context_mapping->gpu_addr();
    return true;
}

bool MsdIntelContext::GetRingbufferGpuAddress(EngineCommandStreamerId id, gpu_addr_t* addr_out)
{
    auto iter = state_map_.find(id);
    if (iter == state_map_.end())
        return DRETF(false, "couldn't find engine command streamer");

    PerEngineState& state = iter->second;
    if (!state.context_mapping)
        return DRETF(false, "context not mapped");

    if (!state.ringbuffer->GetGpuAddress(state.context_mapping->address_space_id(), addr_out))
        return DRETF(false, "failed to get gpu address");

    return true;
}

bool ClientContext::SubmitCommandBuffer(std::unique_ptr<CommandBuffer> cmd_buf)
{
    auto connection = connection_.lock();
    if (!connection)
        return DRETF(false, "couldn't lock reference to connection");

    return connection->SubmitCommandBuffer(std::move(cmd_buf));
}

//////////////////////////////////////////////////////////////////////////////

void msd_context_destroy(msd_context* ctx) { delete MsdIntelAbiContext::cast(ctx); }

magma_status_t msd_context_execute_command_buffer(msd_context* ctx, msd_buffer* cmd_buf,
                                                  msd_buffer** exec_resources)
{
    if (!MsdIntelAbiContext::cast(ctx)->ptr()->SubmitCommandBuffer(
            CommandBuffer::Create(cmd_buf, exec_resources, MsdIntelAbiContext::cast(ctx)->ptr())))
        return DRET(MAGMA_STATUS_INTERNAL_ERROR);
    return MAGMA_STATUS_OK;
}
