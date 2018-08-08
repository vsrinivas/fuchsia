// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_buffer.h"
#include "address_space.h"
#include "engine_command_streamer.h"
#include "instructions.h"
#include "msd_intel_connection.h"
#include "msd_intel_context.h"
#include "msd_intel_semaphore.h"
#include "platform_trace.h"

std::unique_ptr<CommandBuffer> CommandBuffer::Create(msd_buffer_t* abi_cmd_buf,
                                                     msd_buffer_t** msd_buffers,
                                                     std::weak_ptr<ClientContext> context,
                                                     msd_semaphore_t** msd_wait_semaphores,
                                                     msd_semaphore_t** msd_signal_semaphores)
{
    auto command_buffer = std::unique_ptr<CommandBuffer>(
        new CommandBuffer(MsdIntelAbiBuffer::cast(abi_cmd_buf)->ptr(), context));

    if (!command_buffer->Initialize())
        return DRETP(nullptr, "failed to initialize command buffer");

    std::vector<std::shared_ptr<MsdIntelBuffer>> buffers;
    buffers.reserve(command_buffer->num_resources());
    for (uint32_t i = 0; i < command_buffer->num_resources(); i++) {
        buffers.emplace_back(MsdIntelAbiBuffer::cast(msd_buffers[i])->ptr());
    }

    std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores;
    wait_semaphores.reserve(command_buffer->wait_semaphore_count());
    for (uint32_t i = 0; i < command_buffer->wait_semaphore_count(); i++) {
        wait_semaphores.emplace_back(MsdIntelAbiSemaphore::cast(msd_wait_semaphores[i])->ptr());
    }

    std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores;
    signal_semaphores.reserve(command_buffer->signal_semaphore_count());
    for (uint32_t i = 0; i < command_buffer->signal_semaphore_count(); i++) {
        signal_semaphores.emplace_back(MsdIntelAbiSemaphore::cast(msd_signal_semaphores[i])->ptr());
    }

    if (!command_buffer->InitializeResources(std::move(buffers), std::move(wait_semaphores),
                                             std::move(signal_semaphores)))
        return DRETP(nullptr, "failed to initialize command buffer resources");

    return command_buffer;
}

CommandBuffer::CommandBuffer(std::shared_ptr<MsdIntelBuffer> abi_cmd_buf,
                             std::weak_ptr<ClientContext> context)
    : abi_cmd_buf_(std::move(abi_cmd_buf)), context_(context)
{
    nonce_ = TRACE_NONCE();
    prepared_to_execute_ = false;
}

CommandBuffer::~CommandBuffer()
{
    if (!prepared_to_execute_)
        return;

    {
        TRACE_DURATION("magma", "Command Buffer End");
        uint64_t ATTRIBUTE_UNUSED buffer_id = resource(batch_buffer_resource_index()).buffer_id();
        TRACE_FLOW_END("magma", "command_buffer", buffer_id);
    }

    UnmapResourcesGpu();

    for (auto& semaphore : signal_semaphores_) {
        semaphore->Signal();
    }

    std::shared_ptr<MsdIntelConnection> connection = locked_context_->connection().lock();
    if (connection) {
        std::vector<uint64_t> buffer_ids(num_resources());
        for (uint32_t i = 0; i < num_resources(); i++) {
            buffer_ids[i] = exec_resources_[i].buffer->platform_buffer()->id();
        }
        connection->SendNotification(buffer_ids);
    }

    TRACE_ASYNC_END("magma-exec", "CommandBuffer Exec", nonce_);
}

void CommandBuffer::SetSequenceNumber(uint32_t sequence_number)
{
    uint64_t ATTRIBUTE_UNUSED buffer_id = resource(batch_buffer_resource_index()).buffer_id();

    TRACE_ASYNC_BEGIN("magma-exec", "CommandBuffer Exec", nonce_, "id", buffer_id);
    sequence_number_ = sequence_number;
}

bool CommandBuffer::InitializeResources(
    std::vector<std::shared_ptr<MsdIntelBuffer>> buffers,
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores,
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores)
{
    TRACE_DURATION("magma", "InitializeResources");
    if (!magma::CommandBuffer::initialized())
        return DRETF(false, "base command buffer not initialized");

    if (num_resources() != buffers.size())
        return DRETF(false, "buffers size mismatch");

    if (wait_semaphores.size() != wait_semaphore_count())
        return DRETF(false, "wait semaphore count mismatch");

    if (signal_semaphores.size() != signal_semaphore_count())
        return DRETF(false, "wait semaphore count mismatch");

    exec_resources_.clear();
    exec_resources_.reserve(num_resources());
    for (uint32_t i = 0; i < num_resources(); i++) {
        exec_resources_.emplace_back(
            ExecResource{buffers[i], resource(i).offset(), resource(i).length()});
        {
            TRACE_DURATION("magma", "CommitPages");
            uint64_t num_pages = AddressSpace::GetMappedSize(resource(i).length()) >> PAGE_SHIFT;
            DASSERT(magma::is_page_aligned(resource(i).offset()));
            uint64_t page_offset = resource(i).offset() >> PAGE_SHIFT;
            buffers[i]->platform_buffer()->CommitPages(page_offset, num_pages);
        }
    }

    wait_semaphores_ = std::move(wait_semaphores);
    signal_semaphores_ = std::move(signal_semaphores);

    return true;
}

std::weak_ptr<MsdIntelContext> CommandBuffer::GetContext() { return context_; }

uint32_t CommandBuffer::GetPipeControlFlags()
{
    uint32_t flags = MiPipeControl::kCommandStreamerStallEnableBit;

    // Experimentally including this bit has been shown to resolve gpu faults where a batch
    // completes; we clear gtt mappings for resources; then on the next batch,
    // an invalid address is emitted corresponding to a cleared gpu mapping.  This was
    // first seen when a compute shader was introduced.
    flags |= MiPipeControl::kGenericMediaStateClearBit;

    // Similarly, including this bit was shown to resolve the emission of an invalid address.
    flags |= MiPipeControl::kIndirectStatePointersDisableBit;

    // This one is needed when l3 caching enabled via mocs (memory object control state).
    flags |= MiPipeControl::kDcFlushEnableBit;

    return flags;
}

bool CommandBuffer::GetGpuAddress(gpu_addr_t* gpu_addr_out)
{
    if (!prepared_to_execute_)
        return DRETF(false, "not prepared to execute");

    *gpu_addr_out = exec_resource_mappings_[batch_buffer_index_]->gpu_addr() + batch_start_offset_;
    return true;
}

uint64_t CommandBuffer::GetBatchBufferId()
{
    if (batch_buffer_resource_index() < num_resources())
        return resource(batch_buffer_resource_index()).buffer_id();
    return 0;
}

void CommandBuffer::UnmapResourcesGpu() { exec_resource_mappings_.clear(); }

bool CommandBuffer::PrepareForExecution(EngineCommandStreamer* engine,
                                        std::shared_ptr<AddressSpace> global_gtt)
{
    DASSERT(engine);

    locked_context_ = context_.lock();
    if (!locked_context_)
        return DRETF(false, "context has already been deleted, aborting");

    std::shared_ptr<AddressSpace> address_space = locked_context_->exec_address_space();

    if (!locked_context_->IsInitializedForEngine(engine->id())) {
        if (!engine->InitContext(locked_context_.get()))
            return DRETF(false, "failed to initialize context");

        if (!locked_context_->Map(global_gtt, engine->id()))
            return DRETF(false, "failed to map context");

        if (!engine->InitContextCacheConfig(locked_context_))
            return DRETF(false, "failed to init cache config");
    }

    exec_resource_mappings_.clear();
    exec_resource_mappings_.reserve(exec_resources_.size());

    uint64_t ATTRIBUTE_UNUSED buffer_id = resource(batch_buffer_resource_index()).buffer_id();
    TRACE_FLOW_STEP("magma", "command_buffer", buffer_id);

    if (!MapResourcesGpu(address_space, exec_resource_mappings_))
        return DRETF(false, "failed to map execution resources");

    if (!PatchRelocations(exec_resource_mappings_))
        return DRETF(false, "failed to patch relocations");

    for (auto semaphore : signal_semaphores_) {
        semaphore->Reset();
    }

    batch_buffer_index_ = batch_buffer_resource_index();
    batch_start_offset_ = batch_start_offset();

    prepared_to_execute_ = true;
    engine_id_ = engine->id();

    return true;
}

bool CommandBuffer::MapResourcesGpu(std::shared_ptr<AddressSpace> address_space,
                                    std::vector<std::shared_ptr<GpuMapping>>& mappings)
{
    TRACE_DURATION("magma", "MapResourcesGpu");

    for (auto res : exec_resources_) {
        std::shared_ptr<GpuMapping> mapping =
            AddressSpace::GetSharedGpuMapping(address_space, res.buffer, res.offset, res.length);
        if (!mapping)
            return DRETF(false, "failed to map resource into GPU address space");
        DLOG("MapResourcesGpu aspace %p buffer 0x%" PRIx64 " offset 0x%" PRIx64 " length 0x%" PRIx64
             " gpu_addr 0x%" PRIx64,
             address_space.get(), res.buffer->platform_buffer()->id(), res.offset, res.length,
             mapping->gpu_addr());
        mappings.push_back(mapping);
    }

    return true;
}

bool CommandBuffer::PatchRelocation(magma_system_relocation_entry* relocation,
                                    ExecResource* exec_resource, gpu_addr_t target_gpu_address)
{
    DLOG("PatchRelocation offset 0x%x exec_resource offset 0x%lx target_gpu_address 0x%lx "
         "target_offset 0x%x",
         relocation->offset, exec_resource->offset, target_gpu_address, relocation->target_offset);

    TRACE_DURATION("magma", "PatchRelocation");

    uint64_t dst_offset = exec_resource->offset + relocation->offset;

    uint32_t reloc_page_index = dst_offset >> PAGE_SHIFT;
    uint32_t offset_in_page = dst_offset & (PAGE_SIZE - 1);

    DLOG("reloc_page_index 0x%x offset_in_page 0x%x", reloc_page_index, offset_in_page);

    void* buffer_cpu_addr;
    if (!exec_resource->buffer->platform_buffer()->MapCpu(&buffer_cpu_addr))
        return DRETF(false, "failed to map buffer into CPU address space");
    DASSERT(buffer_cpu_addr);

    uint8_t* reloc_page_cpu_addr =
        static_cast<uint8_t*>(buffer_cpu_addr) + reloc_page_index * PAGE_SIZE;

    gpu_addr_t address_to_patch = target_gpu_address + relocation->target_offset;
    static_assert(sizeof(gpu_addr_t) == sizeof(uint64_t), "gpu addr size mismatch");

    memcpy(reloc_page_cpu_addr + offset_in_page, &address_to_patch, sizeof(uint64_t));
    return true;
}

bool CommandBuffer::PatchRelocations(std::vector<std::shared_ptr<GpuMapping>>& mappings)
{
    DASSERT(mappings.size() == num_resources());

    TRACE_DURATION("magma", "PatchRelocations");

    for (uint32_t res_index = 0; res_index < num_resources(); res_index++) {
        auto resource = this->resource(res_index);
        for (uint32_t reloc_index = 0; reloc_index < resource.num_relocations(); reloc_index++) {
            auto reloc = resource.relocation(reloc_index);
            DLOG("Patching relocation res_index %u reloc_index %u target_resource_index %u",
                 res_index, reloc_index, reloc->target_resource_index);
            auto& mapping = mappings[reloc->target_resource_index];
            if (!PatchRelocation(reloc, &exec_resources_[res_index], mapping->gpu_addr()))
                return DRETF(false, "failed to patch relocation");
        }
    }

    return true;
}
