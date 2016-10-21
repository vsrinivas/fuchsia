// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_buffer.h"
#include "engine_command_streamer.h"
#include "msd_intel_context.h"

CommandBuffer::~CommandBuffer()
{
    if (!prepared_to_execute_)
        return;

    UnmapResourcesGpu();

    if (sequence_number_ != Sequencer::kInvalidSequenceNumber) {
        for (auto buf : exec_resources_) {
            // only want to reset seq num on buffers that arent referenced
            // from another more recent command buffer
            if (buf->sequence_number() == sequence_number_) {
                buf->SetSequenceNumber(Sequencer::kInvalidSequenceNumber);
            }
        }
    }
}

void CommandBuffer::SetSequenceNumber(uint32_t sequence_number)
{
    sequence_number_ = sequence_number;
    for (auto buf : exec_resources_) {
        buf->SetSequenceNumber(sequence_number);
    }
}

CommandBuffer::CommandBuffer(magma_system_command_buffer* cmd_buf, msd_buffer** exec_resources,
                             std::weak_ptr<ClientContext> context)
    : cmd_buf_(cmd_buf), context_(context)
{
    exec_resources_.reserve(cmd_buf->num_resources);
    for (uint32_t i = 0; i < cmd_buf->num_resources; i++) {
        exec_resources_.push_back(MsdIntelAbiBuffer::cast(exec_resources[i])->ptr());
    }

    prepared_to_execute_ = false;
}

MsdIntelContext* CommandBuffer::GetContext()
{
    DASSERT(prepared_to_execute_);
    return locked_context_.get();
}

bool CommandBuffer::GetGpuAddress(AddressSpaceId address_space_id, gpu_addr_t* gpu_addr_out)
{
    DASSERT(prepared_to_execute_);
    if (address_space_id != locked_context_->exec_address_space()->id())
        return DRETF(false, "wrong address space");
    *gpu_addr_out = batch_buffer_gpu_addr_;
    return true;
}

void CommandBuffer::UnmapResourcesGpu() { exec_resource_mappings_.clear(); }

bool CommandBuffer::PrepareForExecution(EngineCommandStreamer* engine)
{
    DASSERT(engine);

    locked_context_ = context_.lock();
    if (!locked_context_)
        return DRETF(false, "context has already been deleted, aborting");

    auto address_space = locked_context_->exec_address_space();

    if (!locked_context_->IsInitializedForEngine(engine->id())) {
        if (!engine->InitContext(locked_context_.get()))
            return DRETF(false, "failed to intialize context");
    }

    if (!locked_context_->Map(address_space, engine->id()))
        return DRETF(false, "failed to map context");

    exec_resource_mappings_.clear();
    exec_resource_mappings_.reserve(exec_resources_.size());

    if (!MapResourcesGpu(address_space, exec_resource_mappings_))
        return DRETF(false, "failed to map execution resources");

    if (!PatchRelocations(exec_resource_mappings_))
        return DRETF(false, "failed to patch relocations");

    batch_buffer_gpu_addr_ =
        exec_resource_mappings_[cmd_buf_->batch_buffer_resource_index]->gpu_addr();

    prepared_to_execute_ = true;
    engine_id_ = engine->id();

    return true;
}

bool CommandBuffer::MapResourcesGpu(std::shared_ptr<AddressSpace> address_space,
                                    std::vector<std::shared_ptr<GpuMapping>>& mappings)
{
    for (auto buffer : exec_resources_) {
        std::shared_ptr<GpuMapping> mapping =
            AddressSpace::GetSharedGpuMapping(address_space, buffer, PAGE_SIZE);
        if (!mapping)
            return DRETF(false, "failed to map resource into GPU address space");
        mappings.push_back(mapping);
    }
    return true;
}

bool CommandBuffer::PatchRelocation(magma_system_relocation_entry* relocation,
                                    MsdIntelBuffer* resource, gpu_addr_t target_gpu_address)
{
    // only map the page we need so we dont blow up our memory footprint
    uint32_t reloc_page_index = relocation->offset >> PAGE_SHIFT;
    void* reloc_page_cpu_addr;
    if (!resource->platform_buffer()->MapPageCpu(reloc_page_index, &reloc_page_cpu_addr))
        return DRETF(false, "failed to map relocation page into CPU address space");
    DASSERT(reloc_page_cpu_addr);

    uint32_t offset_in_page = relocation->offset & (PAGE_SIZE - 1);
    gpu_addr_t address_to_patch = target_gpu_address + relocation->target_offset;

    DASSERT(offset_in_page % sizeof(uint32_t) == 0); // just to be sure

    // actually patch the relocation
    static_cast<uint32_t*>(reloc_page_cpu_addr)[offset_in_page / sizeof(uint32_t)] =
        magma::lower_32_bits(address_to_patch);

    offset_in_page += sizeof(uint32_t);

    static_cast<uint32_t*>(reloc_page_cpu_addr)[offset_in_page / sizeof(uint32_t)] =
        magma::upper_32_bits(address_to_patch);

    // unmap the mapped page
    if (!resource->platform_buffer()->UnmapPageCpu(reloc_page_index))
        return DRETF(false, "failed to unmap relocation page from CPU address space");

    return true;
}

bool CommandBuffer::PatchRelocations(std::vector<std::shared_ptr<GpuMapping>>& mappings)
{
    DASSERT(mappings.size() == cmd_buf_->num_resources);

    for (uint32_t res_index = 0; res_index < cmd_buf_->num_resources; res_index++) {
        auto resource = &cmd_buf_->resources[res_index];
        for (uint32_t reloc_index = 0; reloc_index < resource->num_relocations; reloc_index++) {
            auto& mapping = mappings[resource->relocations[reloc_index].target_resource_index];
            if (!PatchRelocation(&resource->relocations[reloc_index],
                                 exec_resources_[res_index].get(), mapping->gpu_addr()))
                return DRETF(false, "failed to patch relocation");
        }
    }

    return true;
}