// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_arm_connection.h"

#include "address_space.h"
#include "gpu_mapping.h"
#include "magma_util/dlog.h"
#include "msd_arm_buffer.h"
#include "msd_arm_context.h"
#include "platform_semaphore.h"

#include <vector>

void msd_connection_close(msd_connection_t* connection)
{
    delete MsdArmAbiConnection::cast(connection);
}

msd_context_t* msd_connection_create_context(msd_connection_t* abi_connection)
{
    auto connection = MsdArmAbiConnection::cast(abi_connection);
    auto context = std::make_unique<MsdArmContext>(connection->ptr());

    return context.release();
}

void msd_context_destroy(struct msd_context_t* ctx)
{
    delete static_cast<MsdArmContext*>(ctx);
}

void msd_connection_present_buffer(msd_connection_t* abi_connection, msd_buffer_t* abi_buffer,
                                   magma_system_image_descriptor* image_desc,
                                   uint32_t wait_semaphore_count, uint32_t signal_semaphore_count,
                                   msd_semaphore_t** semaphores,
                                   msd_present_buffer_callback_t callback, void* callback_data)
{
}

magma_status_t msd_connection_wait_rendering(msd_connection_t* abi_connection, msd_buffer_t* buffer)
{
    return MAGMA_STATUS_INVALID_ARGS;
}

std::unique_ptr<MsdArmConnection> MsdArmConnection::Create(msd_client_id_t client_id)
{
    auto address_space = AddressSpace::Create();
    if (!address_space)
        return DRETP(nullptr, "Couldn't create address space");
    return std::make_unique<MsdArmConnection>(client_id, std::move(address_space));
}

MsdArmConnection::MsdArmConnection(msd_client_id_t client_id,
                                   std::unique_ptr<AddressSpace> address_space)
    : client_id_(client_id), address_space_(std::move(address_space))
{
}

MsdArmConnection::~MsdArmConnection() {}

bool MsdArmConnection::AddMapping(std::unique_ptr<GpuMapping> mapping)
{
    uint64_t gpu_va = mapping->gpu_va();
    if (!magma::is_page_aligned(gpu_va))
        return DRETF(false, "mapping not page aligned");

    if (mapping->size() == 0)
        return DRETF(false, "empty mapping");

    uint64_t start_page = gpu_va >> PAGE_SHIFT;
    if (mapping->size() > (1ul << AddressSpace::kVirtualAddressSize))
        return DRETF(false, "size too large");

    uint64_t page_count = magma::round_up(mapping->size(), PAGE_SIZE) >> PAGE_SHIFT;
    if (start_page + page_count > ((1ul << AddressSpace::kVirtualAddressSize) >> PAGE_SHIFT))
        return DRETF(false, "virtual address too large");

    auto it = gpu_mappings_.upper_bound(gpu_va);
    if (it != gpu_mappings_.end() && (gpu_va + mapping->size() > it->second->gpu_va()))
        return DRETF(false, "Mapping overlaps existing mapping");
    // Find the mapping with the highest VA that's <= this.
    if (it != gpu_mappings_.begin()) {
        --it;
        // Check if the previous mapping overlaps this.
        if (it->second->gpu_va() + it->second->size() > gpu_va)
            return DRETF(false, "Mapping overlaps existing mapping");
    }
    if (!mapping->buffer()->platform_buffer()->PinPages(0, page_count))
        return DRETF(false, "Pages can't be pinned");

    uint64_t access_flags = 0;
    if (mapping->flags() & MAGMA_GPU_MAP_FLAG_READ)
        access_flags |= kAccessFlagRead;
    if (mapping->flags() & MAGMA_GPU_MAP_FLAG_WRITE)
        access_flags |= kAccessFlagWrite;
    if (!(mapping->flags() & MAGMA_GPU_MAP_FLAG_EXECUTE))
        access_flags |= kAccessFlagNoExecute;

    if (mapping->flags() &
        ~(MAGMA_GPU_MAP_FLAG_READ | MAGMA_GPU_MAP_FLAG_WRITE | MAGMA_GPU_MAP_FLAG_EXECUTE))
        return DRETF(false, "Unsupported map flags %lx\n", mapping->flags());

    if (!address_space_->Insert(gpu_va, mapping->buffer()->platform_buffer(), 0, mapping->size(),
                                access_flags)) {
        mapping->buffer()->platform_buffer()->UnpinPages(start_page, page_count);
        return DRETF(false, "Pages can't be inserted into address space");
    }

    gpu_mappings_[gpu_va] = std::move(mapping);
    return true;
}

bool MsdArmConnection::RemoveMapping(uint64_t gpu_va)
{
    auto it = gpu_mappings_.find(gpu_va);
    if (it == gpu_mappings_.end())
        return DRETF(false, "Mapping not found");

    address_space_->Clear(it->second->gpu_va(), it->second->size());

    uint64_t page_count = magma::round_up(it->second->size(), PAGE_SIZE) >> PAGE_SHIFT;
    if (!it->second->buffer()->platform_buffer()->UnpinPages(0, page_count))
        DLOG("Unable to unpin pages");
    gpu_mappings_.erase(gpu_va);
    return true;
}

void msd_connection_map_buffer_gpu(msd_connection_t* abi_connection, msd_buffer_t* abi_buffer,
                                   uint64_t gpu_va, uint64_t page_offset, uint64_t page_count,
                                   uint64_t flags)
{
    MsdArmConnection* connection = MsdArmAbiConnection::cast(abi_connection)->ptr().get();
    std::shared_ptr<MsdArmBuffer> buffer = MsdArmAbiBuffer::cast(abi_buffer)->ptr();

    auto mapping = std::make_unique<GpuMapping>(gpu_va, buffer->platform_buffer()->size(), flags,
                                                connection, buffer);
    connection->AddMapping(std::move(mapping));
}

void msd_connection_unmap_buffer_gpu(msd_connection_t* abi_connection, msd_buffer_t* buffer,
                                     uint64_t gpu_va)
{
    MsdArmAbiConnection::cast(abi_connection)->ptr()->RemoveMapping(gpu_va);
}

void msd_connection_commit_buffer(msd_connection_t* connection, msd_buffer_t* buffer,
                                  uint64_t page_offset, uint64_t page_count)
{
}
