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
    if (outstanding_atoms_[atom_number] &&
        outstanding_atoms_[atom_number]->result_code() == kArmMaliResultRunning) {
        magma::log(magma::LOG_WARNING, "Client %" PRIu64 ": Submitted atom number already in use",
                   client_id_);
        return false;
    }
    uint32_t flags = atom->flags;
    magma_arm_mali_user_data user_data;
    user_data.data[0] = atom->data.data[0];
    user_data.data[1] = atom->data.data[1];
    std::shared_ptr<MsdArmAtom> msd_atom;
    if (flags & kAtomFlagSoftware) {
        if (flags != kAtomFlagSemaphoreSet && flags != kAtomFlagSemaphoreReset &&
            flags != kAtomFlagSemaphoreWait && flags != kAtomFlagSemaphoreWaitAndReset) {
            magma::log(magma::LOG_WARNING, "Client %" PRIu64 ": Invalid soft atom flags 0x%x\n",
                       client_id_, flags);
            return false;
        }
        if (semaphores->empty()) {
            magma::log(magma::LOG_WARNING, "Client %" PRIu64 ": No remaining semaphores",
                       client_id_);
            return false;
        }

        msd_atom =
            std::make_shared<MsdArmSoftAtom>(shared_from_this(), static_cast<AtomFlags>(flags),
                                             semaphores->front(), atom_number, user_data);
        semaphores->pop_front();
    } else {
        uint32_t slot = flags & kAtomFlagRequireFragmentShader ? 0 : 1;
        if (slot == 0 && (flags & (kAtomFlagRequireComputeShader | kAtomFlagRequireTiler))) {
            magma::log(magma::LOG_WARNING, "Client %" PRIu64 ": Invalid atom flags 0x%x\n",
                       client_id_, flags);
            return false;
        }
        msd_atom = std::make_shared<MsdArmAtom>(shared_from_this(), atom->job_chain_addr, slot,
                                                atom_number, user_data);

        if (flags & kAtomFlagRequireCycleCounter)
            msd_atom->set_require_cycle_counter();
    }

    {
        // Hold lock for using outstanding_atoms_.
        std::lock_guard<std::mutex> lock(callback_lock_);

        MsdArmAtom::DependencyList dependencies;
        for (size_t i = 0; i < arraysize(atom->dependencies); i++) {
            uint8_t dependency = atom->dependencies[i].atom_number;
            if (dependency) {
                if (!outstanding_atoms_[dependency]) {
                    magma::log(magma::LOG_WARNING,
                               "Client %" PRIu64
                               ": Dependency on atom that hasn't been submitted yet",
                               client_id_);
                    return false;
                }
                auto type = static_cast<ArmMaliDependencyType>(atom->dependencies[i].type);
                if (type != kArmMaliDependencyOrder && type != kArmMaliDependencyData) {
                    magma::log(magma::LOG_WARNING,
                               "Client %" PRIu64 ": Invalid dependency type: %d", client_id_, type);
                    return false;
                }
                dependencies.push_back(
                    MsdArmAtom::Dependency{type, outstanding_atoms_[dependency]});
            }
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

    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                    "msd_context_execute_command_buffer not implemented");
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
          MAGMA_GPU_MAP_FLAG_GROWABLE | kMagmaArmMaliGpuMapFlagInnerShareable |
          kMagmaArmMaliGpuMapFlagBothShareable))
        return DRETF(false, "Unsupported map flags %lx\n", mapping_flags);

    if (flags_out)
        *flags_out = access_flags;
    return true;
}

bool MsdArmConnection::AddMapping(std::unique_ptr<GpuMapping> mapping)
{
    std::lock_guard<std::mutex> lock(address_lock_);
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
    std::lock_guard<std::mutex> lock(address_lock_);
    auto it = gpu_mappings_.find(gpu_va);
    if (it == gpu_mappings_.end())
        return DRETF(false, "Mapping not found");

    address_space_->Clear(it->second->gpu_va(), it->second->size());

    gpu_mappings_.erase(gpu_va);
    return true;
}

// CommitMemoryForBuffer or PageInAddress will hold address_lock_ before calling this, but that's
// impossible to specify for the thread safety analysis.
bool MsdArmConnection::UpdateCommittedMemory(GpuMapping* mapping) FXL_NO_THREAD_SAFETY_ANALYSIS
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
    if (prev_committed_page_count == committed_page_count) {
        // Sometimes an access to a growable region that was just grown can fault.  Unlock the MMU
        // if that's detected so the access can be retried.
        if (committed_page_count > 0)
            address_space_->Unlock();
        return true;
    }

    if (committed_page_count < prev_committed_page_count) {
        uint64_t pages_to_remove = prev_committed_page_count - committed_page_count;
        address_space_->Clear(mapping->gpu_va() + committed_page_count * PAGE_SIZE,
                              pages_to_remove * PAGE_SIZE);
        mapping->shrink_pinned_pages(pages_to_remove);

    } else {
        uint64_t pages_to_add = committed_page_count - prev_committed_page_count;
        uint64_t page_offset_in_buffer = mapping->page_offset() + prev_committed_page_count;

        std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping =
            owner_->GetBusMapper()->MapPageRangeBus(buffer->platform_buffer(),
                                                    page_offset_in_buffer, pages_to_add);
        if (!bus_mapping)
            return DRETF(false, "Couldn't pin 0x%lx pages", pages_to_add);

        if (!address_space_->Insert(mapping->gpu_va() + prev_committed_page_count * PAGE_SIZE,
                                    bus_mapping.get(), page_offset_in_buffer * PAGE_SIZE,
                                    pages_to_add * PAGE_SIZE, access_flags)) {
            return DRETF(false, "Pages can't be inserted into address space");
        }

        mapping->grow_pinned_pages(std::move(bus_mapping));
    }

    return true;
}

bool MsdArmConnection::PageInMemory(uint64_t address)
{
    std::lock_guard<std::mutex> lock(address_lock_);
    if (gpu_mappings_.empty())
        return false;

    auto it = gpu_mappings_.upper_bound(address);
    if (it == gpu_mappings_.begin())
        return false;
    --it;
    GpuMapping& mapping = *it->second.get();
    DASSERT(address >= mapping.gpu_va());
    if (address >= mapping.gpu_va() + mapping.size())
        return false;
    if (!(mapping.flags() & MAGMA_GPU_MAP_FLAG_GROWABLE))
        return DRETF(false, "Buffer mapping not growable");
    auto buffer = mapping.buffer().lock();
    DASSERT(buffer);

    // TODO(MA-417): Look into growing the buffer on a different thread.

    // Try to grow in units of 64 pages to avoid needing to fault too often.
    constexpr uint64_t kPagesToGrow = 64;
    constexpr uint64_t kCacheLineSize = 64;
    uint64_t offset_needed = address - mapping.gpu_va() + kCacheLineSize - 1;

    // Don't shrink the amount being committed if there's a race and the
    // client committed more memory between when the fault happened and this
    // code.
    uint64_t committed_page_count =
        std::max(buffer->committed_page_count(),
                 magma::round_up(offset_needed, PAGE_SIZE * kPagesToGrow) / PAGE_SIZE);
    committed_page_count =
        std::min(committed_page_count,
                 buffer->platform_buffer()->size() / PAGE_SIZE - buffer->start_committed_pages());

    // The MMU command to update the page tables should automatically cause
    // the atom to continue executing.
    return buffer->SetCommittedPages(buffer->start_committed_pages(), committed_page_count);
}

bool MsdArmConnection::CommitMemoryForBuffer(MsdArmAbiBuffer* buffer, uint64_t page_offset,
                                             uint64_t page_count)
{
    std::lock_guard<std::mutex> lock(address_lock_);
    return GetBuffer(buffer)->SetCommittedPages(page_offset, page_count);
}

void MsdArmConnection::SetNotificationCallback(msd_connection_notification_callback_t callback,
                                               void* token)
{
    std::lock_guard<std::mutex> lock(callback_lock_);
    callback_ = callback;
    token_ = token;
}

void MsdArmConnection::SendNotificationData(MsdArmAtom* atom, ArmMaliResultCode result_code)
{
    std::lock_guard<std::mutex> lock(callback_lock_);
    // It may already have been destroyed on the main thread.
    if (!token_)
        return;

    msd_notification_t notification = {.type = MSD_CONNECTION_NOTIFICATION_CHANNEL_SEND};
    static_assert(sizeof(magma_arm_mali_status) <= MSD_CHANNEL_SEND_MAX_SIZE,
                  "notification too large");
    notification.u.channel_send.size = sizeof(magma_arm_mali_status);

    auto status = reinterpret_cast<magma_arm_mali_status*>(notification.u.channel_send.data);
    status->result_code = result_code;
    status->atom_number = atom->atom_number();
    status->data = atom->user_data();

    callback_(token_, &notification);
}

void MsdArmConnection::MarkDestroyed()
{
    owner_->CancelAtoms(shared_from_this());

    std::lock_guard<std::mutex> lock(callback_lock_);
    if (!token_)
        return;

    msd_notification_t notification = {.type = MSD_CONNECTION_NOTIFICATION_CHANNEL_SEND};
    static_assert(sizeof(magma_arm_mali_status) <= MSD_CHANNEL_SEND_MAX_SIZE,
                  "notification too large");
    notification.u.channel_send.size = sizeof(magma_arm_mali_status);

    auto status = reinterpret_cast<magma_arm_mali_status*>(notification.u.channel_send.data);
    status->result_code = kArmMaliResultTerminated;
    status->atom_number = {};
    status->data = {};

    callback_(token_, &notification);

    // Don't send any completion messages after termination.
    token_ = 0;
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

magma_status_t msd_connection_map_buffer_gpu(msd_connection_t* abi_connection,
                                             msd_buffer_t* abi_buffer, uint64_t gpu_va,
                                             uint64_t page_offset, uint64_t page_count,
                                             uint64_t flags)
{
    TRACE_DURATION("magma", "msd_connection_map_buffer_gpu", "page_count", page_count);
    MsdArmConnection* connection = MsdArmAbiConnection::cast(abi_connection)->ptr().get();
    std::shared_ptr<MsdArmBuffer> buffer = connection->GetBuffer(MsdArmAbiBuffer::cast(abi_buffer));

    auto mapping = std::make_unique<GpuMapping>(gpu_va, page_offset, page_count * PAGE_SIZE, flags,
                                                connection, buffer);
    if (!connection->AddMapping(std::move(mapping)))
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "AddMapping failed");
    return MAGMA_STATUS_OK;
}

magma_status_t msd_connection_unmap_buffer_gpu(msd_connection_t* abi_connection,
                                               msd_buffer_t* buffer, uint64_t gpu_va)
{
    TRACE_DURATION("magma", "msd_connection_unmap_buffer_gpu");
    if (!MsdArmAbiConnection::cast(abi_connection)->ptr()->RemoveMapping(gpu_va))
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "RemoveMapping failed");
    return MAGMA_STATUS_OK;
}

magma_status_t msd_connection_commit_buffer(msd_connection_t* abi_connection,
                                            msd_buffer_t* abi_buffer, uint64_t page_offset,
                                            uint64_t page_count)
{
    MsdArmConnection* connection = MsdArmAbiConnection::cast(abi_connection)->ptr().get();
    if (!connection->CommitMemoryForBuffer(MsdArmAbiBuffer::cast(abi_buffer), page_offset,
                                           page_count))
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "CommitMemoryForBuffer failed");
    return MAGMA_STATUS_OK;
}

void msd_connection_set_notification_callback(msd_connection_t* abi_connection,
                                              msd_connection_notification_callback_t callback,
                                              void* token)
{
    MsdArmAbiConnection::cast(abi_connection)->ptr()->SetNotificationCallback(callback, token);
}

void msd_connection_release_buffer(msd_connection_t* abi_connection,
                                   msd_buffer_t* abi_buffer)
{
    MsdArmConnection* connection = MsdArmAbiConnection::cast(abi_connection)->ptr().get();
    connection->ReleaseBuffer(MsdArmAbiBuffer::cast(abi_buffer));
}
