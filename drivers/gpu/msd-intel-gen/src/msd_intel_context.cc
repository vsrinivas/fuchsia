// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_context.h"
#include "address_space.h"
#include "command_buffer.h"
#include "msd_intel_connection.h"
#include "platform_thread.h"
#include "platform_trace.h"

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

    state.context_mapping = AddressSpace::MapBufferGpu(address_space, state.context_buffer);
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

ClientContext::~ClientContext() { DASSERT(!wait_thread_.joinable()); }

void ClientContext::Shutdown()
{
    if (semaphore_port_)
        semaphore_port_->Close();

    if (wait_thread_.joinable()) {
        DLOG("joining wait thread");
        wait_thread_.join();
        DLOG("joined wait thread");
    }

    semaphore_port_.reset();
}

magma::Status ClientContext::SubmitCommandBuffer(std::unique_ptr<CommandBuffer> command_buffer)
{
    TRACE_DURATION("magma", "ReceiveCommandBuffer");
    uint64_t ATTRIBUTE_UNUSED buffer_id = command_buffer->GetBatchBufferId();
    TRACE_FLOW_STEP("magma", "command_buffer", buffer_id);

    if (killed())
        return DRET(MAGMA_STATUS_CONTEXT_KILLED);

    if (!semaphore_port_) {
        semaphore_port_ = magma::SemaphorePort::Create();

        DASSERT(!wait_thread_.joinable());
        wait_thread_ = std::thread([this] {
            magma::PlatformThreadHelper::SetCurrentThreadName("ConnectionWaitThread");
            DLOG("context wait thread started");
            while (semaphore_port_->WaitOne()) {
            }
            DLOG("context wait thread exited");
        });
    }

    std::unique_lock<std::mutex> lock(pending_command_buffer_mutex_);
    pending_command_buffer_queue_.push(std::move(command_buffer));

    if (pending_command_buffer_queue_.size() == 1)
        return SubmitPendingCommandBuffer(true);

    return MAGMA_STATUS_OK;
}

magma::Status ClientContext::SubmitPendingCommandBuffer(bool have_lock)
{
    auto callback = [this](magma::SemaphorePort::WaitSet* wait_set) {
        this->SubmitPendingCommandBuffer(false);
    };

    auto lock = have_lock
                    ? std::unique_lock<std::mutex>(pending_command_buffer_mutex_, std::adopt_lock)
                    : std::unique_lock<std::mutex>(pending_command_buffer_mutex_);

    while (pending_command_buffer_queue_.size()) {
        DLOG("pending_command_buffer_queue_ size %zu", pending_command_buffer_queue_.size());

        std::unique_ptr<CommandBuffer>& command_buffer = pending_command_buffer_queue_.front();

        // Takes ownership
        auto semaphores = command_buffer->wait_semaphores();

        if (semaphores.size() == 0) {
            auto connection = connection_.lock();
            if (!connection)
                return DRET_MSG(MAGMA_STATUS_CONNECTION_LOST,
                                "couldn't lock reference to connection");

            if (killed())
                return DRET(MAGMA_STATUS_CONTEXT_KILLED);

            {
                TRACE_DURATION("magma", "SubmitCommandBuffer");
                uint64_t ATTRIBUTE_UNUSED buffer_id = command_buffer->GetBatchBufferId();
                TRACE_FLOW_STEP("magma", "command_buffer", buffer_id);
            }
            connection->SubmitCommandBuffer(std::move(command_buffer));
            pending_command_buffer_queue_.pop();
        } else {
            DLOG("adding waitset with %zu semaphores", semaphores.size());

            // Invoke the callback when semaphores are satisfied;
            // the next ProcessPendingFlip will see an empty semaphore array for the front request.
            bool result = semaphore_port_->AddWaitSet(
                std::make_unique<magma::SemaphorePort::WaitSet>(callback, std::move(semaphores)));
            if (result) {
                break;
            } else {
                magma::log(magma::LOG_WARNING,
                           "SubmitPendingCommandBuffer: failed to add to waitset");
            }
        }
    }

    return MAGMA_STATUS_OK;
}

void ClientContext::Kill()
{
    if (killed_)
        return;
    killed_ = true;
    auto connection = connection_.lock();
    if (connection)
        connection->SendContextKilled();
}

//////////////////////////////////////////////////////////////////////////////

void msd_context_destroy(msd_context_t* ctx)
{
    auto abi_context = MsdIntelAbiContext::cast(ctx);
    // get a copy of the shared ptr
    auto client_context = abi_context->ptr();
    client_context->Shutdown();
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

void msd_context_release_buffer(msd_context_t* context, msd_buffer_t* buffer)
{
    auto abi_context = MsdIntelAbiContext::cast(context);
    auto abi_buffer = MsdIntelAbiBuffer::cast(buffer);

    std::shared_ptr<MsdIntelConnection> connection = abi_context->ptr()->connection().lock();
    if (!connection)
        return;

    connection->ReleaseBuffer(abi_buffer->ptr());
}

magma_status_t msd_context_execute_immediate_commands(msd_context_t* ctx, uint64_t commands_size,
                                                      void* commands, uint64_t semaphore_count,
                                                      msd_semaphore_t** msd_semaphores)
{
    return MAGMA_STATUS_CONTEXT_KILLED;
}
