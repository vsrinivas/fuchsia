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
        if (state.context_mapping->address_space().lock() == address_space)
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

bool MsdIntelContext::Unmap(EngineCommandStreamerId id)
{
    auto iter = state_map_.find(id);
    if (iter == state_map_.end())
        return DRETF(false, "couldn't find engine command streamer");

    DLOG("Unmapping context for engine %d", id);

    PerEngineState& state = iter->second;

    if (!state.context_mapping)
        return DRETF(false, "context not mapped");

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

    if (!state.ringbuffer->GetGpuAddress(addr_out))
        return DRETF(false, "failed to get gpu address");

    return true;
}

magma::Status ClientContext::SubmitCommandBuffer(std::unique_ptr<CommandBuffer> cmd_buf)
{
    auto connection = connection_.lock();
    if (!connection)
        return DRET_MSG(MAGMA_STATUS_CONNECTION_LOST, "couldn't lock reference to connection");

    if (connection->context_killed())
        return DRET(MAGMA_STATUS_CONTEXT_KILLED);

    return connection->SubmitCommandBuffer(std::move(cmd_buf));
}

//////////////////////////////////////////////////////////////////////////////

void msd_context_destroy(msd_context_t* ctx)
{
    auto abi_context = MsdIntelAbiContext::cast(ctx);
    // get a copy of the shared ptr
    auto client_context = abi_context->ptr();
    // delete the abi container
    delete abi_context;
    // can safely unmap contexts only from the device thread; for that we go through the connection
    auto connection = client_context->connection().lock();
    DASSERT(connection);
    connection->DestroyContext(std::move(client_context));
}

magma_status_t msd_context_execute_command_buffer(msd_context_t* ctx, msd_buffer_t* cmd_buf,
                                                  msd_buffer_t** exec_resources,
                                                  msd_semaphore_t** wait_semaphores,
                                                  msd_semaphore_t** signal_semaphores)
{
    auto context = MsdIntelAbiContext::cast(ctx)->ptr();

    magma::Status status = context->SubmitCommandBuffer(CommandBuffer::Create(
        cmd_buf, exec_resources, context, wait_semaphores, signal_semaphores));
    return status.get();
}
