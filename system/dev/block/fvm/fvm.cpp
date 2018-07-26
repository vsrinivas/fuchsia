// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <ddk/protocol/block.h>
#include <fbl/array.h>
#include <fbl/atomic.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/limits.h>
#include <fbl/new.h>
#include <lib/fzl/mapped-vmo.h>
#include <lib/sync/completion.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>
#include <zircon/thread_annotations.h>
#include <lib/zx/vmo.h>

#include "fvm-private.h"

namespace fvm {

fbl::unique_ptr<SliceExtent> SliceExtent::Split(size_t vslice) {
    ZX_DEBUG_ASSERT(start() <= vslice);
    ZX_DEBUG_ASSERT(vslice < end());
    fbl::AllocChecker ac;
    fbl::unique_ptr<SliceExtent> new_extent(new (&ac) SliceExtent(vslice + 1));
    if (!ac.check()) {
        return nullptr;
    }
    new_extent->pslices_.reserve(end() - vslice, &ac);
    if (!ac.check()) {
        return nullptr;
    }
    for (size_t vs = vslice + 1; vs < end(); vs++) {
        ZX_ASSERT(new_extent->push_back(get(vs)));
    }
    while (!is_empty() && vslice + 1 != end()) {
        pop_back();
    }
    return fbl::move(new_extent);
}

bool SliceExtent::Merge(const SliceExtent& other) {
    ZX_DEBUG_ASSERT(end() == other.start());
    fbl::AllocChecker ac;
    pslices_.reserve(other.size(), &ac);
    if (!ac.check()) {
        return false;
    }

    for (size_t vs = other.start(); vs < other.end(); vs++) {
        ZX_ASSERT(push_back(other.get(vs)));
    }
    return true;
}

VPartitionManager::VPartitionManager(zx_device_t* parent, const block_info_t& info,
                                     size_t block_op_size, const block_protocol_t* bp)
    : ManagerDeviceType(parent), info_(info), metadata_(nullptr), metadata_size_(0),
      slice_size_(0), block_op_size_(block_op_size) {
    memcpy(&bp_, bp, sizeof(*bp));
}

VPartitionManager::~VPartitionManager() = default;

zx_status_t VPartitionManager::Create(zx_device_t* dev, fbl::unique_ptr<VPartitionManager>* out) {
    block_info_t block_info;
    block_protocol_t bp;
    size_t block_op_size = 0;
    if (device_get_protocol(dev, ZX_PROTOCOL_BLOCK, &bp) != ZX_OK) {
        printf("fvm: ERROR: block device '%s': does not support block protocol\n",
               device_get_name(dev));
        return ZX_ERR_NOT_SUPPORTED;
    }
    bp.ops->query(bp.ctx, &block_info, &block_op_size);

    fbl::AllocChecker ac;
    auto vpm = fbl::make_unique_checked<VPartitionManager>(&ac, dev, block_info,
                                                           block_op_size, &bp);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    *out = fbl::move(vpm);
    return ZX_OK;
}

zx_status_t VPartitionManager::AddPartition(fbl::unique_ptr<VPartition> vp) const {
    auto ename = reinterpret_cast<const char*>(GetAllocatedVPartEntry(vp->GetEntryIndex())->name);
    char name[FVM_NAME_LEN + 32];
    snprintf(name, sizeof(name), "%.*s-p-%zu", FVM_NAME_LEN, ename, vp->GetEntryIndex());

    zx_status_t status;
    if ((status = vp->DdkAdd(name)) != ZX_OK) {
        return status;
    }
    // TODO(johngro): ask smklein why it is OK to release this managed pointer.
    __UNUSED auto ptr = vp.release();
    return ZX_OK;
}

struct VpmIoCookie {
    fbl::atomic<size_t> num_txns;
    fbl::atomic<zx_status_t> status;
    sync_completion_t signal;
};

static void IoCallback(block_op_t* op, zx_status_t status) {
    VpmIoCookie* c = reinterpret_cast<VpmIoCookie*>(op->cookie);
    if (status != ZX_OK) {
        c->status.store(status);
    }
    if (c->num_txns.fetch_sub(1) - 1 == 0) {
        sync_completion_signal(&c->signal);
    }
}

zx_status_t VPartitionManager::DoIoLocked(zx_handle_t vmo, size_t off,
                                          size_t len, uint32_t command) {
    size_t block_size = info_.block_size;
    size_t max_transfer = info_.max_transfer_size / block_size;
    size_t len_remaining = len / block_size;
    size_t vmo_offset = 0;
    size_t dev_offset = off / block_size;
    size_t num_txns = fbl::round_up(len_remaining, max_transfer) / max_transfer;

    fbl::AllocChecker ac;
    fbl::Array<uint8_t> buffer(new (&ac) uint8_t[block_op_size_ * num_txns],
                               block_op_size_ * num_txns);

    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    VpmIoCookie cookie;
    cookie.num_txns.store(num_txns);
    cookie.status.store(ZX_OK);
    sync_completion_reset(&cookie.signal);

    for (size_t i = 0; i < num_txns; i++) {
        size_t length = fbl::min(len_remaining, max_transfer);
        len_remaining -= length;

        block_op_t* bop = reinterpret_cast<block_op_t*>(buffer.get() + (block_op_size_ * i));
        bop->command = command;
        bop->rw.vmo = vmo;
        bop->rw.length = static_cast<uint32_t>(length);
        bop->rw.offset_dev = dev_offset;
        bop->rw.offset_vmo = vmo_offset;
        bop->rw.pages = NULL;
        bop->completion_cb = IoCallback;
        bop->cookie = &cookie;
        memset(buffer.get() + (block_op_size_ * i) + sizeof(block_op_t), 0,
               block_op_size_ - sizeof(block_op_t));
        bp_.ops->queue(bp_.ctx, bop);

        vmo_offset += length;
        dev_offset += length;
    }

    ZX_DEBUG_ASSERT(len_remaining == 0);
    sync_completion_wait(&cookie.signal, ZX_TIME_INFINITE);
    return static_cast<zx_status_t>(cookie.status.load());
}

zx_status_t VPartitionManager::Load() {
    fbl::AutoLock lock(&lock_);

    auto auto_detach = fbl::MakeAutoCall([&]() TA_NO_THREAD_SAFETY_ANALYSIS {
        fprintf(stderr, "fvm: Aborting Driver Load\n");
        DdkRemove();
        // "Load" is running in a background thread called by bind.
        // This thread will be joined when the fvm_device is released,
        // but it must be added to be released.
        //
        // If we fail to initialize anything before it is added,
        // detach the thread and clean up gracefully.
        thrd_detach(init_);
        // Clang's thread analyzer doesn't think we're holding this lock, but
        // we clearly are, and need to release it before deleting the
        // VPartitionManager.
        lock.release();
        delete this;
    });

    zx::vmo vmo;
    if (zx::vmo::create(FVM_BLOCK_SIZE, 0, &vmo) != ZX_OK) {
        return ZX_ERR_INTERNAL;
    }

    // Read the superblock first, to determine the slice sice
    if (DoIoLocked(vmo.get(), 0, FVM_BLOCK_SIZE, BLOCK_OP_READ)) {
        fprintf(stderr, "fvm: Failed to read first block from underlying device\n");
        return ZX_ERR_INTERNAL;
    }

    fvm_t sb;
    zx_status_t status = vmo.read(&sb, 0, sizeof(sb));
    if (status != ZX_OK) {
        return ZX_ERR_INTERNAL;
    }

    // Validate the superblock, confirm the slice size
    slice_size_ = sb.slice_size;
    if ((slice_size_ * VSliceMax()) / VSliceMax() != slice_size_) {
        fprintf(stderr, "fvm: Slice Size, VSliceMax overflow block address space\n");
        return ZX_ERR_BAD_STATE;
    } else if (info_.block_size == 0 || SliceSize() % info_.block_size) {
        fprintf(stderr, "fvm: Bad block (%u) or slice size (%zu)\n",
                info_.block_size, SliceSize());
        return ZX_ERR_BAD_STATE;
    } else if (sb.vpartition_table_size != kVPartTableLength) {
        fprintf(stderr, "fvm: Bad vpartition table size %zu (expected %zu)\n",
                sb.vpartition_table_size, kVPartTableLength);
        return ZX_ERR_BAD_STATE;
    } else if (sb.allocation_table_size != AllocTableLength(DiskSize(), SliceSize())) {
        fprintf(stderr, "fvm: Bad allocation table size %zu (expected %zu)\n",
                sb.allocation_table_size, AllocTableLength(DiskSize(), SliceSize()));
        return ZX_ERR_BAD_STATE;
    }

    metadata_size_ = fvm::MetadataSize(DiskSize(), SliceSize());
    // Now that the slice size is known, read the rest of the metadata
    auto make_metadata_vmo = [&](size_t offset, fbl::unique_ptr<fzl::MappedVmo>* out) {
        fbl::unique_ptr<fzl::MappedVmo> mvmo;
        zx_status_t status = fzl::MappedVmo::Create(MetadataSize(), "fvm-meta", &mvmo);
        if (status != ZX_OK) {
            return status;
        }

        // Read both copies of metadata, ensure at least one is valid
        if ((status = DoIoLocked(mvmo->GetVmo(), offset,
                                 MetadataSize(), BLOCK_OP_READ)) != ZX_OK) {
            return status;
        }

        *out = fbl::move(mvmo);
        return ZX_OK;
    };

    fbl::unique_ptr<fzl::MappedVmo> mvmo;
    if ((status = make_metadata_vmo(0, &mvmo)) != ZX_OK) {
        fprintf(stderr, "fvm: Failed to load metadata vmo: %d\n", status);
        return status;
    }
    fbl::unique_ptr<fzl::MappedVmo> mvmo_backup;
    if ((status = make_metadata_vmo(MetadataSize(), &mvmo_backup)) != ZX_OK) {
        fprintf(stderr, "fvm: Failed to load backup metadata vmo: %d\n", status);
        return status;
    }

    const void* metadata;
    if ((status = fvm_validate_header(mvmo->GetData(), mvmo_backup->GetData(),
                                      MetadataSize(), &metadata)) != ZX_OK) {
        fprintf(stderr, "fvm: Header validation failure: %d\n", status);
        return status;
    }

    if (metadata == mvmo->GetData()) {
        first_metadata_is_primary_ = true;
        metadata_ = fbl::move(mvmo);
    } else {
        first_metadata_is_primary_ = false;
        metadata_ = fbl::move(mvmo_backup);
    }

    // Begin initializing the underlying partitions
    DdkMakeVisible();
    auto_detach.cancel();

    // 0th vpartition is invalid
    fbl::unique_ptr<VPartition> vpartitions[FVM_MAX_ENTRIES] = {};

    // Iterate through FVM Entry table, allocating the VPartitions which
    // claim to have slices.
    for (size_t i = 1; i < FVM_MAX_ENTRIES; i++) {
        if (GetVPartEntryLocked(i)->slices == 0) {
            continue;
        } else if ((status = VPartition::Create(this, i, &vpartitions[i])) != ZX_OK) {
            fprintf(stderr, "FVM: Failed to Create vpartition %zu\n", i);
            return status;
        }
    }

    // Iterate through the Slice Allocation table, filling the slice maps
    // of VPartitions.
    for (uint32_t i = 1; i <= GetFvmLocked()->pslice_count; i++) {
        const slice_entry_t* entry = GetSliceEntryLocked(i);
        if (entry->Vpart() == FVM_SLICE_ENTRY_FREE) {
            continue;
        }
        if (vpartitions[entry->Vpart()] == nullptr) {
            continue;
        }

        // It's fine to load the slices while not holding the vpartition
        // lock; no VPartition devices exist yet.
        vpartitions[entry->Vpart()]->SliceSetUnsafe(entry->Vslice(), i);
    }

    lock.release();

    // Iterate through 'valid' VPartitions, and create their devices.
    size_t device_count = 0;
    for (size_t i = 0; i < FVM_MAX_ENTRIES; i++) {
        if (vpartitions[i] == nullptr) {
            continue;
        } else if (GetAllocatedVPartEntry(i)->flags & kVPartFlagInactive) {
            fprintf(stderr, "FVM: Freeing inactive partition\n");
            FreeSlices(vpartitions[i].get(), 0, VSliceMax());
            continue;
        } else if (AddPartition(fbl::move(vpartitions[i]))) {
            continue;
        }
        device_count++;
    }

    return ZX_OK;
}

zx_status_t VPartitionManager::WriteFvmLocked() {
    zx_status_t status;

    GetFvmLocked()->generation++;
    fvm_update_hash(GetFvmLocked(), MetadataSize());

    // If we were reading from the primary, write to the backup.
    status = DoIoLocked(metadata_->GetVmo(), BackupOffsetLocked(),
                        MetadataSize(), BLOCK_OP_WRITE);
    if (status != ZX_OK) {
        return status;
    }

    // We only allow the switch of "write to the other copy of metadata"
    // once a valid version has been written entirely.
    first_metadata_is_primary_ = !first_metadata_is_primary_;
    return ZX_OK;
}

zx_status_t VPartitionManager::FindFreeVPartEntryLocked(size_t* out) const {
    for (size_t i = 1; i < FVM_MAX_ENTRIES; i++) {
        const vpart_entry_t* entry = GetVPartEntryLocked(i);
        if (entry->slices == 0) {
            *out = i;
            return ZX_OK;
        }
    }
    return ZX_ERR_NO_SPACE;
}

zx_status_t VPartitionManager::FindFreeSliceLocked(size_t* out, size_t hint) const {
    const size_t maxSlices = UsableSlicesCount(DiskSize(), SliceSize());
    hint = fbl::max(hint, 1lu);
    for (size_t i = hint; i <= maxSlices; i++) {
        if (GetSliceEntryLocked(i)->Vpart() == 0) {
            *out = i;
            return ZX_OK;
        }
    }
    for (size_t i = 1; i < hint; i++) {
        if (GetSliceEntryLocked(i)->Vpart() == 0) {
            *out = i;
            return ZX_OK;
        }
    }
    return ZX_ERR_NO_SPACE;
}

zx_status_t VPartitionManager::AllocateSlices(VPartition* vp, size_t vslice_start,
                                              size_t count) {
    fbl::AutoLock lock(&lock_);
    return AllocateSlicesLocked(vp, vslice_start, count);
}

zx_status_t VPartitionManager::AllocateSlicesLocked(VPartition* vp, size_t vslice_start,
                                                    size_t count) {
    if (vslice_start + count > VSliceMax()) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t status = ZX_OK;
    size_t hint = 0;

    {
        fbl::AutoLock lock(&vp->lock_);
        if (vp->IsKilledLocked()) {
            return ZX_ERR_BAD_STATE;
        }
        for (size_t i = 0; i < count; i++) {
            size_t pslice;
            auto vslice = vslice_start + i;
            if (vp->SliceGetLocked(vslice) != PSLICE_UNALLOCATED) {
                status = ZX_ERR_INVALID_ARGS;
            }
            if ((status != ZX_OK) ||
                ((status = FindFreeSliceLocked(&pslice, hint)) != ZX_OK) ||
                ((status = vp->SliceSetLocked(vslice, static_cast<uint32_t>(pslice)) != ZX_OK))) {
                for (int j = static_cast<int>(i - 1); j >= 0; j--) {
                    vslice = vslice_start + j;
                    GetSliceEntryLocked(vp->SliceGetLocked(vslice))->SetVpart(FVM_SLICE_ENTRY_FREE);
                    vp->SliceFreeLocked(vslice);
                }

                return status;
            }
            slice_entry_t* alloc_entry = GetSliceEntryLocked(pslice);
            auto vpart = vp->GetEntryIndex();
            ZX_DEBUG_ASSERT(vpart <= VPART_MAX);
            ZX_DEBUG_ASSERT(vslice <= VSLICE_MAX);
            alloc_entry->SetVpart(vpart);
            alloc_entry->SetVslice(vslice);
            hint = pslice + 1;
        }
    }

    if ((status = WriteFvmLocked()) != ZX_OK) {
        // Undo allocation in the event of failure; avoid holding VPartition
        // lock while writing to fvm.
        fbl::AutoLock lock(&vp->lock_);
        for (int j = static_cast<int>(count - 1); j >= 0; j--) {
            auto vslice = vslice_start + j;
            GetSliceEntryLocked(vp->SliceGetLocked(vslice))->SetVpart(FVM_SLICE_ENTRY_FREE);
            vp->SliceFreeLocked(vslice);
        }
    }

    return status;
}

zx_status_t VPartitionManager::Upgrade(const uint8_t* old_guid, const uint8_t* new_guid) {
    fbl::AutoLock lock(&lock_);
    size_t old_index = 0;
    size_t new_index = 0;

    if (!memcmp(old_guid, new_guid, GUID_LEN)) {
        old_guid = nullptr;
    }

    for (size_t i = 1; i < FVM_MAX_ENTRIES; i++) {
        auto entry = GetVPartEntryLocked(i);
        if (entry->slices != 0) {
            if (old_guid && !(entry->flags & kVPartFlagInactive) &&
                !memcmp(entry->guid, old_guid, GUID_LEN)) {
                old_index = i;
            } else if ((entry->flags & kVPartFlagInactive) &&
                       !memcmp(entry->guid, new_guid, GUID_LEN)) {
                new_index = i;
            }
        }
    }

    if (!new_index) {
        return ZX_ERR_NOT_FOUND;
    }

    if (old_index) {
        GetVPartEntryLocked(old_index)->flags |= kVPartFlagInactive;
    }
    GetVPartEntryLocked(new_index)->flags &= ~kVPartFlagInactive;

    return WriteFvmLocked();
}

zx_status_t VPartitionManager::FreeSlices(VPartition* vp, size_t vslice_start,
                                          size_t count) {
    fbl::AutoLock lock(&lock_);
    return FreeSlicesLocked(vp, vslice_start, count);
}

zx_status_t VPartitionManager::FreeSlicesLocked(VPartition* vp, size_t vslice_start,
                                                size_t count) {
    if (vslice_start + count > VSliceMax() || count > VSliceMax()) {
        return ZX_ERR_INVALID_ARGS;
    }

    bool freed_something = false;
    {
        fbl::AutoLock lock(&vp->lock_);
        if (vp->IsKilledLocked())
            return ZX_ERR_BAD_STATE;

        //TODO: use block protocol
        // Sync first, before removing slices, so iotxns in-flight cannot
        // operate on 'unowned' slices.
        zx_status_t status;
        status = device_ioctl(parent(), IOCTL_DEVICE_SYNC, nullptr, 0, nullptr, 0, nullptr);
        if (status != ZX_OK) {
            return status;
        }

        if (vslice_start == 0) {
            // Special case: Freeing entire VPartition
            for (auto extent = vp->ExtentBegin(); extent.IsValid(); extent = vp->ExtentBegin()) {
                for (size_t i = extent->start(); i < extent->end(); i++) {
                    GetSliceEntryLocked(vp->SliceGetLocked(i))->SetVpart(FVM_SLICE_ENTRY_FREE);
                }
                vp->ExtentDestroyLocked(extent->start());
            }

            // Remove device, VPartition if this was a request to free all slices.
            vp->DdkRemove();
            auto entry = GetVPartEntryLocked(vp->GetEntryIndex());
            entry->clear();
            vp->KillLocked();
            freed_something = true;
        } else {
            for (int i = static_cast<int>(count - 1); i >= 0; i--) {
                auto vslice = vslice_start + i;
                if (vp->SliceCanFree(vslice)) {
                    size_t pslice = vp->SliceGetLocked(vslice);
                    if (!freed_something) {
                        // The first 'free' is the only one which can fail -- it
                        // has the potential to split extents, which may require
                        // memory allocation.
                        if (!vp->SliceFreeLocked(vslice)) {
                            return ZX_ERR_NO_MEMORY;
                        }
                    } else {
                        ZX_ASSERT(vp->SliceFreeLocked(vslice));
                    }
                    GetSliceEntryLocked(pslice)->SetVpart(FVM_SLICE_ENTRY_FREE);
                    freed_something = true;
                }
            }
        }
    }

    if (!freed_something) {
        return ZX_ERR_INVALID_ARGS;
    }
    return WriteFvmLocked();
}

// Device protocol (FVM)

zx_status_t VPartitionManager::DdkIoctl(uint32_t op, const void* cmd,
                                        size_t cmdlen, void* reply, size_t max,
                                        size_t* out_actual) {
    switch (op) {
    case IOCTL_BLOCK_FVM_ALLOC_PARTITION: {
        if (cmdlen < sizeof(alloc_req_t))
            return ZX_ERR_BUFFER_TOO_SMALL;
        const alloc_req_t* request = static_cast<const alloc_req_t*>(cmd);

        if (request->slice_count >= fbl::numeric_limits<uint32_t>::max()) {
            return ZX_ERR_OUT_OF_RANGE;
        } else if (request->slice_count == 0) {
            return ZX_ERR_OUT_OF_RANGE;
        }

        zx_status_t status;
        fbl::unique_ptr<VPartition> vpart;
        {
            fbl::AutoLock lock(&lock_);
            size_t vpart_entry;
            if ((status = FindFreeVPartEntryLocked(&vpart_entry)) != ZX_OK) {
                return status;
            }

            if ((status = VPartition::Create(this, vpart_entry, &vpart)) != ZX_OK) {
                return status;
            }

            auto entry = GetVPartEntryLocked(vpart_entry);
            entry->init(request->type, request->guid,
                        static_cast<uint32_t>(request->slice_count),
                        request->name, request->flags & kVPartAllocateMask);

            if ((status = AllocateSlicesLocked(vpart.get(), 0,
                                               request->slice_count)) != ZX_OK) {
                entry->slices = 0; // Undo VPartition allocation
                return status;
            }
        }
        if ((status = AddPartition(fbl::move(vpart))) != ZX_OK) {
            return status;
        }
        return ZX_OK;
    }
    case IOCTL_BLOCK_FVM_QUERY: {
        if (max < sizeof(fvm_info_t))
            return ZX_ERR_BUFFER_TOO_SMALL;
        fvm_info_t* info = static_cast<fvm_info_t*>(reply);
        info->slice_size = SliceSize();
        info->vslice_count = VSliceMax();
        *out_actual = sizeof(fvm_info_t);
        return ZX_OK;
    }
    case IOCTL_BLOCK_FVM_UPGRADE: {
        if (cmdlen < sizeof(upgrade_req_t)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        const upgrade_req_t* req = static_cast<const upgrade_req_t*>(cmd);
        return Upgrade(req->old_guid, req->new_guid);
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ZX_ERR_NOT_SUPPORTED;
}

void VPartitionManager::DdkUnbind() {
    DdkRemove();
}

void VPartitionManager::DdkRelease() {
    thrd_join(init_, nullptr);
    delete this;
}

VPartition::VPartition(VPartitionManager* vpm, size_t entry_index, size_t block_op_size)
    : PartitionDeviceType(vpm->zxdev()), mgr_(vpm), entry_index_(entry_index) {

    memcpy(&info_, &mgr_->info_, sizeof(block_info_t));
    info_.block_count = 0;
}

VPartition::~VPartition() = default;

zx_status_t VPartition::Create(VPartitionManager* vpm, size_t entry_index,
                               fbl::unique_ptr<VPartition>* out) {
    ZX_DEBUG_ASSERT(entry_index != 0);

    fbl::AllocChecker ac;
    auto vp = fbl::make_unique_checked<VPartition>(&ac, vpm, entry_index, vpm->BlockOpSize());
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    *out = fbl::move(vp);
    return ZX_OK;
}

uint32_t VPartition::SliceGetLocked(size_t vslice) const {
    ZX_DEBUG_ASSERT(vslice < mgr_->VSliceMax());
    auto extent = --slice_map_.upper_bound(vslice);
    if (!extent.IsValid()) {
        return 0;
    }
    ZX_DEBUG_ASSERT(extent->start() <= vslice);
    return extent->get(vslice);
}

zx_status_t VPartition::CheckSlices(size_t vslice_start, size_t* count, bool* allocated) {
    fbl::AutoLock lock(&lock_);

    if (vslice_start >= mgr_->VSliceMax()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    if (IsKilledLocked()) {
        return ZX_ERR_BAD_STATE;
    }

    *count = 0;
    *allocated = false;

    auto extent = --slice_map_.upper_bound(vslice_start);
    if (extent.IsValid()) {
        ZX_DEBUG_ASSERT(extent->start() <= vslice_start);
        if (extent->start() + extent->size() > vslice_start) {
            *count = extent->size() - (vslice_start - extent->start());
            *allocated = true;
        }
    }

    if (!(*allocated)) {
        auto extent = slice_map_.upper_bound(vslice_start);
        if (extent.IsValid()) {
            ZX_DEBUG_ASSERT(extent->start() > vslice_start);
            *count = extent->start() - vslice_start;
        } else {
            *count = mgr_->VSliceMax() - vslice_start;
        }
    }

    return ZX_OK;
}

zx_status_t VPartition::SliceSetLocked(size_t vslice, uint32_t pslice) {
    ZX_DEBUG_ASSERT(vslice < mgr_->VSliceMax());
    auto extent = --slice_map_.upper_bound(vslice);
    ZX_DEBUG_ASSERT(!extent.IsValid() || extent->get(vslice) == PSLICE_UNALLOCATED);
    if (extent.IsValid() && (vslice == extent->end())) {
        // Easy case: append to existing slice
        if (!extent->push_back(pslice)) {
            return ZX_ERR_NO_MEMORY;
        }
    } else {
        // Longer case: there is no extent for this vslice, so we should make
        // one.
        fbl::AllocChecker ac;
        fbl::unique_ptr<SliceExtent> new_extent(new (&ac) SliceExtent(vslice));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        } else if (!new_extent->push_back(pslice)) {
            return ZX_ERR_NO_MEMORY;
        }
        ZX_DEBUG_ASSERT(new_extent->GetKey() == vslice);
        ZX_DEBUG_ASSERT(new_extent->get(vslice) == pslice);
        slice_map_.insert(fbl::move(new_extent));
        extent = --slice_map_.upper_bound(vslice);
    }

    ZX_DEBUG_ASSERT(SliceGetLocked(vslice) == pslice);
    AddBlocksLocked((mgr_->SliceSize() / info_.block_size));

    // Merge with the next contiguous extent (if any)
    auto nextExtent = slice_map_.upper_bound(vslice);
    if (nextExtent.IsValid() && (vslice + 1 == nextExtent->start())) {
        if (extent->Merge(*nextExtent)) {
            slice_map_.erase(*nextExtent);
        }
    }

    return ZX_OK;
}

bool VPartition::SliceFreeLocked(size_t vslice) {
    ZX_DEBUG_ASSERT(vslice < mgr_->VSliceMax());
    ZX_DEBUG_ASSERT(SliceCanFree(vslice));
    auto extent = --slice_map_.upper_bound(vslice);
    if (vslice != extent->end() - 1) {
        // Removing from the middle of an extent; this splits the extent in
        // two.
        auto new_extent = extent->Split(vslice);
        if (new_extent == nullptr) {
            return false;
        }
        slice_map_.insert(fbl::move(new_extent));
    }
    // Removing from end of extent
    extent->pop_back();
    if (extent->is_empty()) {
        slice_map_.erase(*extent);
    }

    AddBlocksLocked(-(mgr_->SliceSize() / info_.block_size));
    return true;
}

void VPartition::ExtentDestroyLocked(size_t vslice) TA_REQ(lock_) {
    ZX_DEBUG_ASSERT(vslice < mgr_->VSliceMax());
    ZX_DEBUG_ASSERT(SliceCanFree(vslice));
    auto extent = --slice_map_.upper_bound(vslice);
    size_t length = extent->size();
    slice_map_.erase(*extent);
    AddBlocksLocked(-((length * mgr_->SliceSize()) / info_.block_size));
}

static zx_status_t RequestBoundCheck(const extend_request_t* request,
                                     size_t vslice_max) {
    if (request->offset == 0 || request->offset > vslice_max) {
        return ZX_ERR_OUT_OF_RANGE;
    } else if (request->length > vslice_max) {
        return ZX_ERR_OUT_OF_RANGE;
    } else if (request->offset + request->length < request->offset ||
               request->offset + request->length > vslice_max) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    return ZX_OK;
}

// Device protocol (VPartition)

zx_status_t VPartition::DdkIoctl(uint32_t op, const void* cmd, size_t cmdlen,
                                 void* reply, size_t max, size_t* out_actual) {
    switch (op) {
    case IOCTL_BLOCK_GET_INFO: {
        block_info_t* info = static_cast<block_info_t*>(reply);
        if (max < sizeof(*info))
            return ZX_ERR_BUFFER_TOO_SMALL;
        fbl::AutoLock lock(&lock_);
        if (IsKilledLocked())
            return ZX_ERR_BAD_STATE;
        memcpy(info, &info_, sizeof(*info));
        *out_actual = sizeof(*info);
        return ZX_OK;
    }
    case IOCTL_BLOCK_FVM_VSLICE_QUERY: {
        if (cmdlen < sizeof(query_request_t)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }

        if (max < sizeof(query_response_t)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }

        const query_request_t* request = static_cast<const query_request_t*>(cmd);

        if (request->count > MAX_FVM_VSLICE_REQUESTS) {
           return ZX_ERR_BUFFER_TOO_SMALL;
        }

        query_response_t* response = static_cast<query_response_t*>(reply);
        response->count = 0;
        for (size_t i = 0; i < request->count; i++) {
            zx_status_t status;
            if ((status = CheckSlices(request->vslice_start[i], &response->vslice_range[i].count,
                                      &response->vslice_range[i].allocated)) != ZX_OK) {
                return status;
            }
            response->count++;
        }

        *out_actual = sizeof(query_response_t);
        return ZX_OK;
    }
    case IOCTL_BLOCK_FVM_QUERY: {
        if (max < sizeof(fvm_info_t))
            return ZX_ERR_BUFFER_TOO_SMALL;
        fvm_info_t* info = static_cast<fvm_info_t*>(reply);
        info->slice_size = mgr_->SliceSize();
        info->vslice_count = mgr_->VSliceMax();
        *out_actual = sizeof(fvm_info_t);
        return ZX_OK;
    }
    case IOCTL_BLOCK_GET_TYPE_GUID: {
        char* guid = static_cast<char*>(reply);
        if (max < FVM_GUID_LEN)
            return ZX_ERR_BUFFER_TOO_SMALL;
        fbl::AutoLock lock(&lock_);
        if (IsKilledLocked())
            return ZX_ERR_BAD_STATE;
        memcpy(guid, mgr_->GetAllocatedVPartEntry(entry_index_)->type, FVM_GUID_LEN);
        *out_actual = FVM_GUID_LEN;
        return ZX_OK;
    }
    case IOCTL_BLOCK_GET_PARTITION_GUID: {
        char* guid = static_cast<char*>(reply);
        if (max < FVM_GUID_LEN)
            return ZX_ERR_BUFFER_TOO_SMALL;
        fbl::AutoLock lock(&lock_);
        if (IsKilledLocked())
            return ZX_ERR_BAD_STATE;
        memcpy(guid, mgr_->GetAllocatedVPartEntry(entry_index_)->guid, FVM_GUID_LEN);
        *out_actual = FVM_GUID_LEN;
        return ZX_OK;
    }
    case IOCTL_BLOCK_GET_NAME: {
        char* name = static_cast<char*>(reply);
        if (max < FVM_NAME_LEN + 1)
            return ZX_ERR_BUFFER_TOO_SMALL;
        fbl::AutoLock lock(&lock_);
        if (IsKilledLocked())
            return ZX_ERR_BAD_STATE;
        memcpy(name, mgr_->GetAllocatedVPartEntry(entry_index_)->name, FVM_NAME_LEN);
        name[FVM_NAME_LEN] = 0;
        *out_actual = strlen(name);
        return ZX_OK;
    }
    case IOCTL_DEVICE_SYNC: {
        // Propagate sync to parent device
        return device_ioctl(GetParent(), IOCTL_DEVICE_SYNC, nullptr, 0, nullptr, 0, nullptr);
    }
    case IOCTL_BLOCK_FVM_EXTEND: {
        if (cmdlen < sizeof(extend_request_t))
            return ZX_ERR_BUFFER_TOO_SMALL;
        const extend_request_t* request = static_cast<const extend_request_t*>(cmd);
        zx_status_t status;
        if ((status = RequestBoundCheck(request, mgr_->VSliceMax())) != ZX_OK) {
            return status;
        } else if (request->length == 0) {
            return ZX_OK;
        }
        return mgr_->AllocateSlices(this, request->offset, request->length);
    }
    case IOCTL_BLOCK_FVM_SHRINK: {
        if (cmdlen < sizeof(extend_request_t))
            return ZX_ERR_BUFFER_TOO_SMALL;
        const extend_request_t* request = static_cast<const extend_request_t*>(cmd);
        zx_status_t status;
        if ((status = RequestBoundCheck(request, mgr_->VSliceMax())) != ZX_OK) {
            return status;
        } else if (request->length == 0) {
            return ZX_OK;
        }
        return mgr_->FreeSlices(this, request->offset, request->length);
    }
    case IOCTL_BLOCK_FVM_DESTROY_PARTITION: {
        return mgr_->FreeSlices(this, 0, mgr_->VSliceMax());
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

typedef struct multi_txn_state {
    multi_txn_state(size_t total, block_op_t* txn)
        : txns_completed(0), txns_total(total), status(ZX_OK), original(txn) {}

    fbl::Mutex lock;
    size_t txns_completed TA_GUARDED(lock);
    size_t txns_total TA_GUARDED(lock);
    zx_status_t status TA_GUARDED(lock);
    block_op_t* original TA_GUARDED(lock);
} multi_txn_state_t;

static void multi_txn_completion(block_op_t* txn, zx_status_t status) {
    multi_txn_state_t* state = static_cast<multi_txn_state_t*>(txn->cookie);
    bool last_txn = false;
    {
        fbl::AutoLock lock(&state->lock);
        state->txns_completed++;
        if (state->status == ZX_OK && status != ZX_OK) {
            state->status = status;
        }
        if (state->txns_completed == state->txns_total) {
            last_txn = true;
            state->original->completion_cb(state->original, state->status);
        }
    }

    if (last_txn) {
        delete state;
    }
    delete[] txn;
}

void VPartition::BlockQueue(block_op_t* txn) {
    ZX_DEBUG_ASSERT(mgr_->BlockOpSize() > 0);
    switch (txn->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
        break;
    // Pass-through operations
    case BLOCK_OP_FLUSH:
        mgr_->Queue(txn);
        return;
    default:
        fprintf(stderr, "[FVM BlockQueue] Unsupported Command: %x\n", txn->command);
        txn->completion_cb(txn, ZX_ERR_NOT_SUPPORTED);
        return;
    }

    const uint64_t device_capacity = DdkGetSize() / BlockSize();
    if (txn->rw.length == 0) {
        txn->completion_cb(txn, ZX_ERR_INVALID_ARGS);
        return;
    } else if ((txn->rw.offset_dev >= device_capacity) ||
               (device_capacity - txn->rw.offset_dev < txn->rw.length)) {
        txn->completion_cb(txn, ZX_ERR_OUT_OF_RANGE);
        return;
    }

    const size_t disk_size = mgr_->DiskSize();
    const size_t slice_size = mgr_->SliceSize();
    const uint64_t blocks_per_slice = slice_size / BlockSize();
    // Start, end both inclusive
    size_t vslice_start = txn->rw.offset_dev / blocks_per_slice;
    size_t vslice_end = (txn->rw.offset_dev + txn->rw.length - 1) / blocks_per_slice;

    fbl::AutoLock lock(&lock_);
    if (vslice_start == vslice_end) {
        // Common case: txn occurs within one slice
        uint32_t pslice = SliceGetLocked(vslice_start);
        if (pslice == PSLICE_UNALLOCATED) {
            txn->completion_cb(txn, ZX_ERR_OUT_OF_RANGE);
            return;
        }
        txn->rw.offset_dev = SliceStart(disk_size, slice_size, pslice) /
                BlockSize() + (txn->rw.offset_dev % blocks_per_slice);
        mgr_->Queue(txn);
        return;
    }

    // Less common case: txn spans multiple slices

    // First, check that all slices are allocated.
    // If any are missing, then this txn will fail.
    bool contiguous = true;
    for (size_t vslice = vslice_start; vslice <= vslice_end; vslice++) {
        if (SliceGetLocked(vslice) == PSLICE_UNALLOCATED) {
            txn->completion_cb(txn, ZX_ERR_OUT_OF_RANGE);
            return;
        }
        if (vslice != vslice_start && SliceGetLocked(vslice - 1) + 1 != SliceGetLocked(vslice)) {
            contiguous = false;
        }
    }

    // Ideal case: slices are contiguous
    if (contiguous) {
        uint32_t pslice = SliceGetLocked(vslice_start);
        txn->rw.offset_dev = SliceStart(disk_size, slice_size, pslice) /
                BlockSize() + (txn->rw.offset_dev % blocks_per_slice);
        mgr_->Queue(txn);
        return;
    }

    // Harder case: Noncontiguous slices
    const size_t txn_count = vslice_end - vslice_start + 1;
    fbl::Vector<block_op_t*> txns;
    txns.reserve(txn_count);

    fbl::AllocChecker ac;
    fbl::unique_ptr<multi_txn_state_t> state(new (&ac) multi_txn_state_t(txn_count, txn));
    if (!ac.check()) {
        txn->completion_cb(txn, ZX_ERR_NO_MEMORY);
        return;
    }

    uint32_t length_remaining = txn->rw.length;
    for (size_t i = 0; i < txn_count; i++) {
        size_t vslice = vslice_start + i;
        uint32_t pslice = SliceGetLocked(vslice);

        uint64_t offset_vmo = txn->rw.offset_vmo;
        uint64_t length;
        if (vslice == vslice_start) {
            length = fbl::round_up(txn->rw.offset_dev + 1, blocks_per_slice) - txn->rw.offset_dev;
        } else if (vslice == vslice_end) {
            length = length_remaining;
            offset_vmo += txn->rw.length - length_remaining;
        } else {
            length = blocks_per_slice;
            offset_vmo += txns[0]->rw.length + blocks_per_slice * (i - 1);
        }
        ZX_DEBUG_ASSERT(length <= blocks_per_slice);
        ZX_DEBUG_ASSERT(length <= length_remaining);

        txns.push_back(reinterpret_cast<block_op_t*>(new uint8_t[mgr_->BlockOpSize()]));
        if (txns[i] == nullptr) {
            while (i-- > 0) {
                delete[] txns[i];
            }
            txn->completion_cb(txn, ZX_ERR_NO_MEMORY);
            return;
        }
        memcpy(txns[i], txn, sizeof(*txn));
        txns[i]->rw.offset_vmo = offset_vmo;
        txns[i]->rw.length = static_cast<uint32_t>(length);
        txns[i]->rw.offset_dev = SliceStart(disk_size, slice_size, pslice) / BlockSize();
        if (vslice == vslice_start) {
            txns[i]->rw.offset_dev += (txn->rw.offset_dev % blocks_per_slice);
        }
        length_remaining -= txns[i]->rw.length;
        txns[i]->completion_cb = multi_txn_completion;
        txns[i]->cookie = state.get();
    }
    ZX_DEBUG_ASSERT(length_remaining == 0);

    for (size_t i = 0; i < txn_count; i++) {
        mgr_->Queue(txns[i]);
    }
    // TODO(johngro): ask smklein why it is OK to release this managed pointer.
    __UNUSED auto ptr = state.release();
}

zx_off_t VPartition::DdkGetSize() {
    const zx_off_t sz = mgr_->VSliceMax() * mgr_->SliceSize();
    // Check for overflow; enforced when loading driver
    ZX_DEBUG_ASSERT(sz / mgr_->VSliceMax() == mgr_->SliceSize());
    return sz;
}

void VPartition::DdkUnbind() {
    DdkRemove();
}

void VPartition::DdkRelease() {
    delete this;
}

void VPartition::BlockQuery(block_info_t* info_out, size_t* block_op_size_out) {
    static_assert(fbl::is_same<decltype(info_out), decltype(&info_)>::value, "Info type mismatch");
    memcpy(info_out, &info_, sizeof(info_));
    *block_op_size_out = mgr_->BlockOpSize();
}

} // namespace fvm

// C-compatibility definitions

static zx_status_t fvm_load_thread(void* arg) {
    return reinterpret_cast<fvm::VPartitionManager*>(arg)->Load();
}

zx_status_t fvm_bind(zx_device_t* parent) {
    fbl::unique_ptr<fvm::VPartitionManager> vpm;
    zx_status_t status = fvm::VPartitionManager::Create(parent, &vpm);
    if (status != ZX_OK) {
        return status;
    }
    if ((status = vpm->DdkAdd("fvm", DEVICE_ADD_INVISIBLE)) != ZX_OK) {
        return status;
    }

    // Read vpartition table asynchronously
    status = thrd_create_with_name(&vpm->init_, fvm_load_thread, vpm.get(), "fvm-init");
    if (status < 0) {
        vpm->DdkRemove();
        return status;
    }
    // TODO(johngro): ask smklein why it is OK to release this managed pointer.
    __UNUSED auto ptr = vpm.release();
    return ZX_OK;
}
