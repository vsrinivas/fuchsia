// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_buffer.h"
#include "magma_connection.h"

MagmaBuffer::MagmaBuffer(MagmaConnection* connection, const char* name, uint32_t alignment)
    : connection_(connection), refcount_(new BufferRefcount(name, this)), alignment_(alignment)
{
    magic_ = kMagic;

    // Reserve the maximum number of relocations you will possibly need so the
    // vector never gets resized. We don't have any real evidence that resizing
    // the relocation vector introduces any meaningful performance overhead but
    // a certain someone is convinced that it could be bad so we do this to be safe.
    uint32_t max_relocations = connection->batch_size() / sizeof(uint32_t) / 2 - 2;
    relocations_.reserve(max_relocations);
}

MagmaBuffer::~MagmaBuffer()
{
    DLOG("~MagmaBuffer");
    magma_system_free(connection_->sys_connection(), this->handle);

    this->handle = 0xdeadbeef;
    this->size = 0;
}

bool MagmaBuffer::Alloc(uint64_t size)
{
    uint32_t handle;
    if (!magma_system_alloc(connection_->sys_connection(), size, &size, &handle))
        return false;

    this->handle = static_cast<uint32_t>(handle);
    this->size = size;
    this->virt = nullptr;
    this->offset64 = 0;

    return true;
}

bool MagmaBuffer::Export(uint32_t* token_out)
{
    return connection_->ExportBufferObject(this, token_out);
}

void MagmaBuffer::SetTilingMode(uint32_t tiling_mode)
{
    if (magma_system_set_tiling_mode(connection_->sys_connection(), this->handle, tiling_mode))
        tiling_mode_ = tiling_mode;
}

bool MagmaBuffer::Map(bool write)
{
    void* addr;
    if (!magma_system_map(connection_->sys_connection(), this->handle, &addr))
        return false;

    this->virt = addr;

    if (!magma_system_set_domain(connection_->sys_connection(), this->handle, MAGMA_DOMAIN_CPU,
                                 write ? MAGMA_DOMAIN_CPU : 0))
        return false;

    return true;
}

bool MagmaBuffer::Unmap()
{
    if (!magma_system_unmap(connection_->sys_connection(), this->handle, this->virt))
        return false;

    this->virt = nullptr;
    return true;
}

void MagmaBuffer::WaitRendering()
{
    return magma_system_wait_rendering(connection_->sys_connection(), this->handle);
}

void MagmaBuffer::EmitRelocation(uint32_t offset, MagmaBuffer* target, uint32_t target_offset,
                                 uint32_t read_domains_bitfield, uint32_t write_domains_bitfield)
{
    relocations_.emplace_back(offset, target, target_offset, read_domains_bitfield,
                              write_domains_bitfield);
}

void MagmaBuffer::GetAbiExecResource(std::set<MagmaBuffer*>& resources,
                                     magma_system_exec_resource* abi_res_out,
                                     magma_system_relocation_entry* relocations_out)
{
    abi_res_out->buffer_handle = handle;

    abi_res_out->num_relocations = relocations_.size();
    abi_res_out->relocations = relocations_out;
    uint32_t i = 0;
    for (auto relocation : relocations_) {
        auto abi_reloc = &abi_res_out->relocations[i++];
        relocation.GetAbiRelocationEntry(abi_reloc);
        auto iter = resources.find(relocation.target());
        DASSERT(iter != resources.end());
        abi_reloc->target_resource_index = std::distance(resources.begin(), iter);
    }
}

void MagmaBuffer::GenerateExecResourceSet(std::set<MagmaBuffer*>& resources)
{
    resources.insert(this);
    for (auto relocation : relocations_) {
        DASSERT(relocation.target());
        // only recur if we havent already seen the node to handle cycles safely
        if (resources.find(relocation.target()) == resources.end()) {
            relocation.target()->GenerateExecResourceSet(resources);
        }
    }
}

std::unique_ptr<MagmaBuffer::CommandBuffer> MagmaBuffer::PrepareForExecution()
{
    std::set<MagmaBuffer*> resources;
    GenerateExecResourceSet(resources);
    auto iter = resources.find(this);
    DASSERT(iter != resources.end());
    uint32_t res_index = std::distance(resources.begin(), iter);

    return std::unique_ptr<CommandBuffer>(new CommandBuffer(res_index, resources));
}

bool MagmaBuffer::References(MagmaBuffer* target)
{
    for (auto relocation : relocations_) {
        if (relocation.target() == target ||
            (relocation.target() != this && relocation.target()->References(target)))
            return true;
    }
    return false;
}

// we do all the memory allocations here so that we have the option of allocating contiguous
// memory if needed for IPC
MagmaBuffer::CommandBuffer::CommandBuffer(uint32_t batch_buffer_resource_index,
                                          std::set<MagmaBuffer*>& resources)
{
    cmd_buf_ = new magma_system_command_buffer();
    cmd_buf_->batch_buffer_resource_index = batch_buffer_resource_index;
    cmd_buf_->num_resources = resources.size();
    cmd_buf_->resources = new magma_system_exec_resource[resources.size()];
    uint32_t i = 0;
    for (auto resource : resources) {
        auto relocations = new magma_system_relocation_entry[resource->RelocationCount()];
        resource->GetAbiExecResource(resources, &cmd_buf_->resources[i++], relocations);
    }
}
MagmaBuffer::CommandBuffer::~CommandBuffer()
{
    for (uint32_t i = 0; i < cmd_buf_->num_resources; i++) {
        delete cmd_buf_->resources[i].relocations;
    }
    delete cmd_buf_->resources;
    delete cmd_buf_;
}