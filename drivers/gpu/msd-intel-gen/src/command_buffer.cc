// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_buffer.h"
#include "engine_command_streamer.h"
#include "instructions.h"
#include "msd_intel_context.h"
#include "msd_intel_semaphore.h"

CommandBuffer::~CommandBuffer()
{
    for (auto res : exec_resources_) {
        res.buffer->DecrementInflightCounter();
    }

    if (!prepared_to_execute_)
        return;

    UnmapResourcesGpu();

    for (auto& semaphore : signal_semaphores_) {
        semaphore->Signal();
    }
}

void CommandBuffer::SetSequenceNumber(uint32_t sequence_number)
{
    sequence_number_ = sequence_number;
}

CommandBuffer::CommandBuffer(msd_buffer* abi_cmd_buf, std::weak_ptr<ClientContext> context)
    : abi_cmd_buf_(MsdIntelAbiBuffer::cast(abi_cmd_buf)->ptr()), context_(context)
{
    prepared_to_execute_ = false;
}

bool CommandBuffer::Initialize(msd_buffer** exec_buffers, msd_semaphore** wait_semaphores,
                               msd_semaphore** signal_semaphores)
{
    if (!magma::CommandBuffer::Initialize())
        return DRETF(false, "Failed to intialize command buffer base class");

    exec_resources_.reserve(num_resources());
    for (uint32_t i = 0; i < num_resources(); i++) {
        auto buffer = MsdIntelAbiBuffer::cast(exec_buffers[i])->ptr();
        exec_resources_.emplace_back(
            ExecResource{buffer, resource(i).offset(), resource(i).length()});
    }

    wait_semaphores_.reserve(wait_semaphore_count());
    for (uint32_t i = 0; i < wait_semaphore_count(); i++) {
        wait_semaphores_.emplace_back(MsdIntelAbiSemaphore::cast(wait_semaphores[i])->ptr());
    }

    signal_semaphores_.reserve(signal_semaphore_count());
    for (uint32_t i = 0; i < signal_semaphore_count(); i++) {
        signal_semaphores_.emplace_back(MsdIntelAbiSemaphore::cast(signal_semaphores[i])->ptr());
    }

    for (auto res : exec_resources_) {
        res.buffer->IncrementInflightCounter();
    }

    return true;
}

std::weak_ptr<MsdIntelContext> CommandBuffer::GetContext() { return context_; }

uint32_t CommandBuffer::GetPipeControlFlags()
{
    return MiPipeControl::kIndirectStatePointersDisable |
           MiPipeControl::kCommandStreamerStallEnableBit;
}

bool CommandBuffer::GetGpuAddress(gpu_addr_t* gpu_addr_out)
{
    if (!prepared_to_execute_)
        return DRETF(false, "not prepared to execute");

    *gpu_addr_out = exec_resource_mappings_[batch_buffer_index_]->gpu_addr() + batch_start_offset_;
    return true;
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
    }

    if (!locked_context_->Map(global_gtt, engine->id()))
        return DRETF(false, "failed to map context");

    exec_resource_mappings_.clear();
    exec_resource_mappings_.reserve(exec_resources_.size());

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
    for (auto res : exec_resources_) {
        std::shared_ptr<GpuMapping> mapping = AddressSpace::GetSharedGpuMapping(
            address_space, res.buffer, res.offset, res.length, PAGE_SIZE);
        if (!mapping)
            return DRETF(false, "failed to map resource into GPU address space");
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

    uint64_t dst_offset = exec_resource->offset + relocation->offset;

    uint32_t reloc_page_index = dst_offset >> PAGE_SHIFT;
    uint32_t offset_in_page = dst_offset & (PAGE_SIZE - 1);

    DLOG("reloc_page_index 0x%x offset_in_page 0x%x", reloc_page_index, offset_in_page);

    void* reloc_page_cpu_addr;
    if (!exec_resource->buffer->platform_buffer()->MapPageCpu(reloc_page_index,
                                                              &reloc_page_cpu_addr))
        return DRETF(false, "failed to map relocation page into CPU address space");

    DASSERT(reloc_page_cpu_addr);

    gpu_addr_t address_to_patch = target_gpu_address + relocation->target_offset;

    DASSERT(offset_in_page % sizeof(uint32_t) == 0); // just to be sure

    // actually patch the relocation
    DASSERT(offset_in_page < PAGE_SIZE);
    static_cast<uint32_t*>(reloc_page_cpu_addr)[offset_in_page / sizeof(uint32_t)] =
        magma::lower_32_bits(address_to_patch);

    offset_in_page += sizeof(uint32_t);

    if (offset_in_page >= PAGE_SIZE) {
        if (!exec_resource->buffer->platform_buffer()->UnmapPageCpu(reloc_page_index))
            return DRETF(false, "failed to unmap relocation page from CPU address space");

        reloc_page_index++;
        if (!exec_resource->buffer->platform_buffer()->MapPageCpu(reloc_page_index,
                                                                  &reloc_page_cpu_addr))
            return DRETF(false, "failed to map relocation page into CPU address space");

        offset_in_page = 0;
    }

    DASSERT(offset_in_page < PAGE_SIZE);
    static_cast<uint32_t*>(reloc_page_cpu_addr)[offset_in_page / sizeof(uint32_t)] =
        magma::upper_32_bits(address_to_patch);

    if (!exec_resource->buffer->platform_buffer()->UnmapPageCpu(reloc_page_index))
        return DRETF(false, "failed to unmap relocation page from CPU address space");

    return true;
}

bool CommandBuffer::PatchRelocations(std::vector<std::shared_ptr<GpuMapping>>& mappings)
{
    DASSERT(mappings.size() == num_resources());

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