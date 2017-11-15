// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_arm_connection.h"

#include <limits>
#include <vector>

#include "address_space.h"
#include "gpu_mapping.h"
#include "lib/fxl/arraysize.h"
#include "magma_arm_mali_types.h"
#include "magma_util/dlog.h"
#include "msd_arm_buffer.h"
#include "msd_arm_context.h"
#include "msd_arm_device.h"
#include "msd_arm_semaphore.h"
#include "platform_semaphore.h"
#include "platform_trace.h"

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

void msd_context_destroy(msd_context_t* ctx)
{
    auto context = static_cast<MsdArmContext*>(ctx);
    auto connection = context->connection().lock();
    if (connection)
        connection->MarkDestroyed();
    delete context;
}

bool MsdArmConnection::ExecuteAtom(
    volatile magma_arm_mali_atom* atom,
    std::deque<std::shared_ptr<magma::PlatformSemaphore>>* semaphores)
{
    uint8_t atom_number = atom->atom_number;
    uint32_t flags = atom->flags;
    magma_arm_mali_user_data user_data;
    user_data.data[0] = atom->data.data[0];
    user_data.data[1] = atom->data.data[1];
    std::shared_ptr<MsdArmAtom> msd_atom;
    if (flags & kAtomFlagSoftware) {
        if (flags != kAtomFlagSemaphoreSet && flags != kAtomFlagSemaphoreReset &&
            flags != kAtomFlagSemaphoreWait && flags != kAtomFlagSemaphoreWaitAndReset) {
            magma::log(magma::LOG_WARNING, "Invalid soft atom flags 0x%x\n", flags);
            return false;
        }
        if (semaphores->empty()) {
            magma::log(magma::LOG_WARNING, "No remaining semaphores");
            return false;
        }

        msd_atom =
            std::make_shared<MsdArmSoftAtom>(shared_from_this(), static_cast<AtomFlags>(flags),
                                             semaphores->front(), atom_number, user_data);
        semaphores->pop_front();
    } else {
        uint32_t slot = flags & kAtomFlagRequireFragmentShader ? 0 : 1;
        if (slot == 0 && (flags & (kAtomFlagRequireComputeShader | kAtomFlagRequireTiler))) {
            magma::log(magma::LOG_WARNING, "Invalid atom flags 0x%x\n", flags);
            return false;
        }
        msd_atom = std::make_shared<MsdArmAtom>(shared_from_this(), atom->job_chain_addr, slot,
                                                atom_number, user_data);
    }

    {
        // Hold lock for using outstanding_atoms_.
        std::lock_guard<std::mutex> lock(channel_lock_);

        MsdArmAtom::DependencyList dependencies;
        for (size_t i = 0; i < arraysize(atom->dependencies); i++) {
            uint8_t dependency = atom->dependencies[i];
            if (dependency)
                dependencies.push_back(outstanding_atoms_[dependency]);
        }
        msd_atom->set_dependencies(dependencies);

        static_assert(arraysize(outstanding_atoms_) - 1 ==
                          std::numeric_limits<decltype(magma_arm_mali_atom::atom_number)>::max(),
                      "outstanding_atoms_ size is incorrect");

        outstanding_atoms_[atom_number] = msd_atom;
    }
    owner_->ScheduleAtom(std::move(msd_atom));
    return true;
}

magma_status_t msd_context_execute_command_buffer(msd_context_t* ctx, msd_buffer_t* cmd_buf,
                                                  msd_buffer_t** exec_resources,
                                                  msd_semaphore_t** wait_semaphores,
                                                  msd_semaphore_t** signal_semaphores)
{
    TRACE_DURATION("magma", "msd_context_execute_command_buffer");
    auto context = static_cast<MsdArmContext*>(ctx);
    auto connection = context->connection().lock();
    if (!connection)
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Connection not valid");

    std::deque<std::shared_ptr<magma::PlatformSemaphore>> semaphores;
    // Command buffers aren't shared cross-connection, so use the base
    // pointer.
    auto command_buffer = MsdArmAbiBuffer::cast(cmd_buf)->base_ptr();
    void* command_buffer_addr;
    if (!command_buffer->platform_buffer()->MapCpu(&command_buffer_addr))
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Can't map buffer");
    auto* command_buffer_data = static_cast<magma_system_command_buffer*>(command_buffer_addr);
    for (size_t i = 0; i < command_buffer_data->signal_semaphore_count; i++) {
        semaphores.push_back(MsdArmAbiSemaphore::cast(signal_semaphores[i])->ptr());
    }

    command_buffer->platform_buffer()->UnmapCpu();

    auto buffer = connection->GetBuffer(MsdArmAbiBuffer::cast(exec_resources[0]));
    void* addr;
    if (!buffer->platform_buffer()->MapCpu(&addr))
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Can't map buffer");

    if (buffer->platform_buffer()->size() < sizeof(uint64_t)) {
        buffer->platform_buffer()->UnmapCpu();
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Buffer too small");
    }
    // This is marked as volatile to ensure the compiler only dereferences it
    // once, so that the client can't increase the atom count after it's been
    // validated and have the loop dereference memory outside of the buffer.
    volatile uint64_t* atom_count_ptr = static_cast<volatile uint64_t*>(addr);
    uint64_t atom_count = *atom_count_ptr;
    TRACE_DURATION("magma", "atom count", "atom_count", atom_count);

    uint64_t buffer_max_entries =
        (buffer->platform_buffer()->size() - sizeof(uint64_t)) / sizeof(magma_arm_mali_atom);
    if (buffer_max_entries < atom_count) {
        buffer->platform_buffer()->UnmapCpu();
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Buffer too small");
    }

    volatile magma_arm_mali_atom* atom =
        reinterpret_cast<volatile magma_arm_mali_atom*>(atom_count_ptr + 1);
    for (uint64_t i = 0; i < atom_count; i++) {
        if (!connection->ExecuteAtom(&atom[i], &semaphores)) {
            buffer->platform_buffer()->UnmapCpu();
            return DRET(MAGMA_STATUS_CONTEXT_KILLED);
        }
    }

    buffer->platform_buffer()->UnmapCpu();

    return MAGMA_STATUS_OK;
}

magma_status_t msd_context_execute_immediate_commands(msd_context_t* ctx, uint64_t commands_size,
                                                      void* commands, uint64_t semaphore_count,
                                                      msd_semaphore_t** msd_semaphores)
{
    auto context = static_cast<MsdArmContext*>(ctx);
    auto connection = context->connection().lock();
    if (!connection)
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Connection not valid");

    size_t count = commands_size / sizeof(magma_arm_mali_atom);
    magma_arm_mali_atom* atoms = static_cast<magma_arm_mali_atom*>(commands);
    std::deque<std::shared_ptr<magma::PlatformSemaphore>> semaphores;
    for (size_t i = 0; i < semaphore_count; i++) {
        semaphores.push_back(MsdArmAbiSemaphore::cast(msd_semaphores[i])->ptr());
    }
    for (size_t i = 0; i < count; i++) {
        if (!connection->ExecuteAtom(&atoms[i], &semaphores))
            return DRET(MAGMA_STATUS_CONTEXT_KILLED);
    }

    return MAGMA_STATUS_OK;
}

magma_status_t msd_connection_wait_rendering(msd_connection_t* abi_connection, msd_buffer_t* buffer)
{
    return MAGMA_STATUS_INVALID_ARGS;
}

std::shared_ptr<MsdArmConnection> MsdArmConnection::Create(msd_client_id_t client_id, Owner* owner)
{
    auto connection = std::shared_ptr<MsdArmConnection>(new MsdArmConnection(client_id, owner));
    if (!connection->Init())
        return DRETP(nullptr, "Couldn't create connection");
    return connection;
}

bool MsdArmConnection::Init()
{
    // If coherent memory is supported, use it for page tables to avoid
    // unnecessary cache flushes.
    address_space_ =
        AddressSpace::Create(this, owner_->cache_coherency_status() == kArmMaliCacheCoherencyAce);
    if (!address_space_)
        return DRETF(false, "Couldn't create address space");
    return true;
}

MsdArmConnection::MsdArmConnection(msd_client_id_t client_id, Owner* owner)
    : client_id_(client_id), owner_(owner)
{
}

MsdArmConnection::~MsdArmConnection() { DASSERT(buffers_.empty()); }

static bool access_flags_from_flags(uint64_t mapping_flags, bool cache_coherent,
                                    uint64_t* flags_out)
{
    uint64_t access_flags = 0;
    if (mapping_flags & MAGMA_GPU_MAP_FLAG_READ)
        access_flags |= kAccessFlagRead;
    if (mapping_flags & MAGMA_GPU_MAP_FLAG_WRITE)
        access_flags |= kAccessFlagWrite;
    if (!(mapping_flags & MAGMA_GPU_MAP_FLAG_EXECUTE))
        access_flags |= kAccessFlagNoExecute;
    if (mapping_flags & kMagmaArmMaliGpuMapFlagInnerShareable)
        access_flags |= kAccessFlagShareInner;
    if (mapping_flags & kMagmaArmMaliGpuMapFlagBothShareable) {
        if (!cache_coherent)
            return DRETF(false, "Attempting to use cache coherency while disabled.");
        access_flags |= kAccessFlagShareBoth;
    }
    if (mapping_flags &
        ~(MAGMA_GPU_MAP_FLAG_READ | MAGMA_GPU_MAP_FLAG_WRITE | MAGMA_GPU_MAP_FLAG_EXECUTE |
          kMagmaArmMaliGpuMapFlagInnerShareable | kMagmaArmMaliGpuMapFlagBothShareable))
        return DRETF(false, "Unsupported map flags %lx\n", mapping_flags);

    if (flags_out)
        *flags_out = access_flags;
    return true;
}

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
    auto buffer = mapping->buffer().lock();
    DASSERT(buffer);

    if (mapping->page_offset() + page_count > buffer->platform_buffer()->size() / PAGE_SIZE)
        return DRETF(false, "Buffer size %lx too small for map start %lx count %lx",
                     buffer->platform_buffer()->size(), mapping->page_offset(), page_count);

    if (!access_flags_from_flags(mapping->flags(),
                                 owner_->cache_coherency_status() == kArmMaliCacheCoherencyAce,
                                 nullptr))
        return false;

    if (!UpdateCommittedMemory(mapping.get()))
        return false;
    gpu_mappings_[gpu_va] = std::move(mapping);
    return true;
}

bool MsdArmConnection::RemoveMapping(uint64_t gpu_va)
{
    auto it = gpu_mappings_.find(gpu_va);
    if (it == gpu_mappings_.end())
        return DRETF(false, "Mapping not found");

    address_space_->Clear(it->second->gpu_va(), it->second->size());

    auto buffer = it->second->buffer().lock();
    if (buffer) {
        bool unpin_success = buffer->platform_buffer()->UnpinPages(it->second->page_offset(),
                                                                   it->second->pinned_page_count());
        DASSERT(unpin_success);
    }
    gpu_mappings_.erase(gpu_va);
    return true;
}

bool MsdArmConnection::UpdateCommittedMemory(GpuMapping* mapping)
{
    uint64_t access_flags = 0;
    if (!access_flags_from_flags(mapping->flags(),
                                 owner_->cache_coherency_status() == kArmMaliCacheCoherencyAce,
                                 &access_flags))
        return false;

    auto buffer = mapping->buffer().lock();
    DASSERT(buffer);

    if (buffer->start_committed_pages() != mapping->page_offset() &&
        (buffer->committed_page_count() > 0 || mapping->pinned_page_count() > 0))
        return DRETF(false, "start of commit should match page offset");

    uint64_t prev_committed_page_count = mapping->pinned_page_count();
    DASSERT(prev_committed_page_count <= mapping->size() / PAGE_SIZE);
    uint64_t committed_page_count =
        std::min(buffer->committed_page_count(), mapping->size() / PAGE_SIZE);
    if (prev_committed_page_count == committed_page_count)
        return true;

    if (committed_page_count < prev_committed_page_count) {
        uint64_t pages_to_remove = prev_committed_page_count - committed_page_count;
        uint64_t page_offset_in_buffer = mapping->page_offset() + committed_page_count;
        address_space_->Clear(mapping->gpu_va() + committed_page_count * PAGE_SIZE,
                              pages_to_remove * PAGE_SIZE);
        bool unpin_success =
            buffer->platform_buffer()->UnpinPages(page_offset_in_buffer, pages_to_remove);
        DASSERT(unpin_success);
        mapping->set_pinned_page_count(committed_page_count);
    } else {
        uint64_t pages_to_add = committed_page_count - prev_committed_page_count;
        uint64_t page_offset_in_buffer = mapping->page_offset() + prev_committed_page_count;
        if (!buffer->platform_buffer()->PinPages(page_offset_in_buffer, pages_to_add))
            return DRETF(false, "Pages can't be pinned");
        if (!address_space_->Insert(mapping->gpu_va() + prev_committed_page_count * PAGE_SIZE,
                                    buffer->platform_buffer(), page_offset_in_buffer * PAGE_SIZE,
                                    pages_to_add * PAGE_SIZE, access_flags)) {
            bool unpin_success =
                buffer->platform_buffer()->UnpinPages(page_offset_in_buffer, pages_to_add);
            DASSERT(unpin_success);
            return DRETF(false, "Pages can't be inserted into address space");
        }
        mapping->set_pinned_page_count(committed_page_count);
    }
    return true;
}

void MsdArmConnection::SetNotificationChannel(msd_channel_send_callback_t send_callback,
                                              msd_channel_t channel)
{
    std::lock_guard<std::mutex> lock(channel_lock_);
    send_callback_ = send_callback;
    return_channel_ = channel;
}

void MsdArmConnection::SendNotificationData(MsdArmAtom* atom, ArmMaliResultCode status)
{
    std::lock_guard<std::mutex> lock(channel_lock_);
    outstanding_atoms_[atom->atom_number()].reset();
    // It may already have been destroyed on the main thread.
    if (!return_channel_)
        return;
    magma_arm_mali_status data;
    data.data = atom->user_data();
    data.result_code = status;
    data.atom_number = atom->atom_number();

    send_callback_(return_channel_, &data, sizeof(data));
}

void MsdArmConnection::MarkDestroyed()
{
    owner_->CancelAtoms(shared_from_this());

    std::lock_guard<std::mutex> lock(channel_lock_);
    if (!return_channel_)
        return;
    struct magma_arm_mali_status data = {};
    data.result_code = kArmMaliResultTerminated;

    send_callback_(return_channel_, &data, sizeof(data));

    // Don't send any completion messages after termination.
    return_channel_ = 0;
}

std::shared_ptr<MsdArmBuffer> MsdArmConnection::GetBuffer(MsdArmAbiBuffer* buffer)
{
    auto it = buffers_.find(buffer);
    if (it != buffers_.end())
        return it->second;

    auto cloned_buffer = buffer->CloneBuffer();
    buffers_[buffer] = cloned_buffer;
    return cloned_buffer;
}

void MsdArmConnection::ReleaseBuffer(MsdArmAbiBuffer* buffer)
{
    // A per-connection buffer may not have been retrieved, so this may erase
    // nothing.
    buffers_.erase(buffer);
}

void msd_connection_map_buffer_gpu(msd_connection_t* abi_connection, msd_buffer_t* abi_buffer,
                                   uint64_t gpu_va, uint64_t page_offset, uint64_t page_count,
                                   uint64_t flags)
{
    TRACE_DURATION("magma", "msd_connection_map_buffer_gpu", "page_count", page_count);
    MsdArmConnection* connection = MsdArmAbiConnection::cast(abi_connection)->ptr().get();
    std::shared_ptr<MsdArmBuffer> buffer = connection->GetBuffer(MsdArmAbiBuffer::cast(abi_buffer));

    auto mapping = std::make_unique<GpuMapping>(gpu_va, page_offset, page_count * PAGE_SIZE, flags,
                                                connection, buffer);
    connection->AddMapping(std::move(mapping));
}

void msd_connection_unmap_buffer_gpu(msd_connection_t* abi_connection, msd_buffer_t* buffer,
                                     uint64_t gpu_va)
{
    TRACE_DURATION("magma", "msd_connection_unmap_buffer_gpu");
    MsdArmAbiConnection::cast(abi_connection)->ptr()->RemoveMapping(gpu_va);
}

void msd_connection_commit_buffer(msd_connection_t* abi_connection, msd_buffer_t* abi_buffer,
                                  uint64_t page_offset, uint64_t page_count)
{
    MsdArmConnection* connection = MsdArmAbiConnection::cast(abi_connection)->ptr().get();
    connection->GetBuffer(MsdArmAbiBuffer::cast(abi_buffer))
        ->SetCommittedPages(page_offset, page_count);
}

void msd_connection_set_notification_channel(msd_connection_t* abi_connection,
                                             msd_channel_send_callback_t send_callback,
                                             msd_channel_t notification_channel)
{
    auto connection = MsdArmAbiConnection::cast(abi_connection)->ptr();
    connection->SetNotificationChannel(send_callback, notification_channel);
}

void msd_connection_release_buffer(msd_connection_t* abi_connection,
                                   msd_buffer_t* abi_buffer)
{
    MsdArmConnection* connection = MsdArmAbiConnection::cast(abi_connection)->ptr().get();
    connection->ReleaseBuffer(MsdArmAbiBuffer::cast(abi_buffer));
}
