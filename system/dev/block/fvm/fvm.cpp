// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <fs/mapped-vmo.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>
#include <zircon/thread_annotations.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/limits.h>
#include <fbl/new.h>
#include <threads.h>

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

VPartitionManager::VPartitionManager(zx_device_t* parent, const block_info_t& info)
    : ManagerDeviceType(parent), info_(info), metadata_(nullptr), metadata_size_(0),
      slice_size_(0) {}

VPartitionManager::~VPartitionManager() = default;

zx_status_t VPartitionManager::Create(zx_device_t* dev, fbl::unique_ptr<VPartitionManager>* out) {
    block_info_t block_info;
    size_t actual = 0;
    ssize_t rc = device_ioctl(dev, IOCTL_BLOCK_GET_INFO, nullptr, 0, &block_info,
                              sizeof(block_info), &actual);
    if (rc < 0) {
        return static_cast<zx_status_t>(rc);
    } else if (actual != sizeof(block_info)) {
        return ZX_ERR_BAD_STATE;
    } else if (block_info.block_size == 0) {
        return ZX_ERR_BAD_STATE;
    }

    fbl::AllocChecker ac;
    auto vpm = fbl::make_unique_checked<VPartitionManager>(&ac, dev, block_info);
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
    vp.release();
    return ZX_OK;
}

zx_status_t VPartitionManager::Load() {
    fbl::AutoLock lock(&lock_);

    auto auto_detach = fbl::MakeAutoCall([&]() TA_NO_THREAD_SAFETY_ANALYSIS {
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

    // Read the superblock first, to determine the slice sice
    iotxn_t* txn = nullptr;
    zx_status_t status = iotxn_alloc(&txn, IOTXN_ALLOC_POOL, FVM_BLOCK_SIZE);
    if (status != ZX_OK) {
        return status;
    }
    txn->opcode = IOTXN_OP_READ;
    txn->offset = 0;
    txn->length = FVM_BLOCK_SIZE;
    iotxn_synchronous_op(parent(), txn);
    if (txn->status != ZX_OK) {
        status = txn->status;
        iotxn_release(txn);
        return status;
    }

    fvm_t sb;
    iotxn_copyfrom(txn, &sb, sizeof(sb), 0);
    iotxn_release(txn);

    // Validate the superblock, confirm the slice size
    slice_size_ = sb.slice_size;
    if (info_.block_size == 0 || SliceSize() % info_.block_size) {
        return ZX_ERR_BAD_STATE;
    } else if (sb.vpartition_table_size != kVPartTableLength) {
        return ZX_ERR_BAD_STATE;
    } else if (sb.allocation_table_size != AllocTableLength(DiskSize(), SliceSize())) {
        return ZX_ERR_BAD_STATE;
    }

    metadata_size_ = fvm::MetadataSize(DiskSize(), SliceSize());
    // Now that the slice size is known, read the rest of the metadata
    auto make_metadata_vmo = [&](size_t offset, fbl::unique_ptr<MappedVmo>* out) {
        fbl::unique_ptr<MappedVmo> mvmo;
        zx_status_t status = MappedVmo::Create(MetadataSize(), "fvm-meta", &mvmo);
        if (status != ZX_OK) {
            return status;
        }

        // Read both copies of metadata, ensure at least one is valid
        status = iotxn_alloc_vmo(&txn, IOTXN_ALLOC_POOL, mvmo->GetVmo(), 0, MetadataSize());
        if (status != ZX_OK) {
            return status;
        }
        txn->opcode = IOTXN_OP_READ;
        txn->offset = offset;
        txn->length = MetadataSize();
        iotxn_synchronous_op(parent(), txn);
        if (txn->status != ZX_OK) {
            status = txn->status;
            iotxn_release(txn);
            return status;
        }
        iotxn_release(txn);
        *out = fbl::move(mvmo);
        return ZX_OK;
    };

    fbl::unique_ptr<MappedVmo> mvmo;
    if ((status = make_metadata_vmo(0, &mvmo)) != ZX_OK) {
        return status;
    }
    fbl::unique_ptr<MappedVmo> mvmo_backup;
    if ((status = make_metadata_vmo(MetadataSize(), &mvmo_backup)) != ZX_OK) {
        return status;
    }

    const void* metadata;
    if ((status = fvm_validate_header(mvmo->GetData(), mvmo_backup->GetData(),
                                      MetadataSize(), &metadata)) != ZX_OK) {
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
    if ((status = DdkAdd("fvm")) != ZX_OK) {
        return status;
    }
    auto_detach.cancel();

    // 0th vpartition is invalid
    fbl::unique_ptr<VPartition> vpartitions[FVM_MAX_ENTRIES] = {};

    // Iterate through FVM Entry table, allocating the VPartitions which
    // claim to have slices.
    for (size_t i = 1; i < FVM_MAX_ENTRIES; i++) {
        if (GetVPartEntryLocked(i)->slices == 0) {
            continue;
        } else if ((status = VPartition::Create(this, i, &vpartitions[i])) != ZX_OK) {
            return status;
        }
    }

    // Iterate through the Slice Allocation table, filling the slice maps
    // of VPartitions.
    for (uint32_t i = 1; i <= GetFvmLocked()->pslice_count; i++) {
        const slice_entry_t* entry = GetSliceEntryLocked(i);
        if (entry->vpart == FVM_SLICE_FREE) {
            continue;
        }
        if (vpartitions[entry->vpart] == nullptr) {
            return ZX_ERR_BAD_STATE;
        }

        // It's fine to load the slices while not holding the vpartition
        // lock; no VPartition devices exist yet.
        vpartitions[entry->vpart]->SliceSetUnsafe(entry->vslice, i);
    }

    lock.release();

    // Iterate through 'valid' VPartitions, and create their devices.
    size_t device_count = 0;
    for (size_t i = 0; i < FVM_MAX_ENTRIES; i++) {
        if (vpartitions[i] == nullptr) {
            continue;
        }
        if (AddPartition(fbl::move(vpartitions[i]))) {
            continue;
        }
        device_count++;
    }

    return ZX_OK;
}

zx_status_t VPartitionManager::WriteFvmLocked() {
    iotxn_t* txn = nullptr;

    zx_status_t status = iotxn_alloc_vmo(&txn, IOTXN_ALLOC_POOL,
                                         metadata_->GetVmo(), 0,
                                         MetadataSize());
    if (status != ZX_OK) {
        return status;
    }
    txn->opcode = IOTXN_OP_WRITE;
    // If we were reading from the primary, write to the backup.
    txn->offset = BackupOffsetLocked();
    txn->length = MetadataSize();

    GetFvmLocked()->generation++;
    fvm_update_hash(GetFvmLocked(), MetadataSize());

    iotxn_synchronous_op(parent_, txn);
    status = txn->status;
    iotxn_release(txn);
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
        if (GetSliceEntryLocked(i)->vpart == 0) {
            *out = i;
            return ZX_OK;
        }
    }
    for (size_t i = 1; i < hint; i++) {
        if (GetSliceEntryLocked(i)->vpart == 0) {
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
        if (vp->IsKilledLocked())
            return ZX_ERR_BAD_STATE;
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
                    GetSliceEntryLocked(vp->SliceGetLocked(vslice))->vpart = PSLICE_UNALLOCATED;
                    vp->SliceFreeLocked(vslice);
                }

                return status;
            }
            slice_entry_t* alloc_entry = GetSliceEntryLocked(pslice);
            auto vpart = vp->GetEntryIndex();
            ZX_DEBUG_ASSERT(vpart <= VPART_MAX);
            ZX_DEBUG_ASSERT(vslice <= VSLICE_MAX);
            alloc_entry->vpart = vpart & VPART_MAX;
            alloc_entry->vslice = vslice & VSLICE_MAX;
            hint = pslice + 1;
        }
    }

    if ((status = WriteFvmLocked()) != ZX_OK) {
        // Undo allocation in the event of failure; avoid holding VPartition
        // lock while writing to fvm.
        fbl::AutoLock lock(&vp->lock_);
        for (int j = static_cast<int>(count - 1); j >= 0; j--) {
            auto vslice = vslice_start + j;
            GetSliceEntryLocked(vp->SliceGetLocked(vslice))->vpart = PSLICE_UNALLOCATED;
            vp->SliceFreeLocked(vslice);
        }
    }
    return status;
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
                while (!extent->is_empty()) {
                    auto vslice = extent->end() - 1;
                    GetSliceEntryLocked(vp->SliceGetLocked(vslice))->vpart = PSLICE_UNALLOCATED;
                    ZX_ASSERT(vp->SliceFreeLocked(vslice));
                }
            }

            // Remove device, VPartition if this was a request to free all slices.
            device_remove(vp->zxdev());
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
                    GetSliceEntryLocked(pslice)->vpart = 0;
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
    case IOCTL_BLOCK_FVM_ALLOC: {
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
                        request->name);

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
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ZX_ERR_NOT_SUPPORTED;
}

void VPartitionManager::DdkUnbind() {
    device_remove(zxdev());
}

void VPartitionManager::DdkRelease() {
    thrd_join(init_, nullptr);
    delete this;
}

VPartition::VPartition(VPartitionManager* vpm, size_t entry_index)
    : PartitionDeviceType(vpm->zxdev()), mgr_(vpm), entry_index_(entry_index) {

    memcpy(&info_, &mgr_->info_, sizeof(block_info_t));
    info_.block_count = 0;
}

VPartition::~VPartition() = default;

zx_status_t VPartition::Create(VPartitionManager* vpm, size_t entry_index,
                               fbl::unique_ptr<VPartition>* out) {
    ZX_DEBUG_ASSERT(entry_index != 0);

    fbl::AllocChecker ac;
    auto vp = fbl::make_unique_checked<VPartition>(&ac, vpm, entry_index);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    *out = fbl::move(vp);
    return ZX_OK;
}

static void vpart_block_complete(iotxn_t* txn, void* cookie) {
    block_callbacks_t* cb;
    memcpy(&cb, txn->extra, sizeof(void*));
    cb->complete(cookie, txn->status);
    iotxn_release(txn);
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

    if (vslice_start > mgr_->VSliceMax()) {
        return ZX_ERR_INVALID_ARGS;
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

void VPartition::Txn(uint32_t opcode, zx_handle_t vmo, uint64_t length,
                     uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    zx_status_t status;
    iotxn_t* txn;
    if ((status = iotxn_alloc_vmo(&txn, IOTXN_ALLOC_POOL, vmo, vmo_offset,
                                  length)) != ZX_OK) {
        callbacks_->complete(cookie, status);
        return;
    }
    txn->opcode = opcode;
    txn->offset = dev_offset;
    txn->complete_cb = vpart_block_complete;
    txn->cookie = cookie;
    memcpy(txn->extra, &callbacks_, sizeof(void*));
    iotxn_queue(zxdev(), txn);
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
    case IOCTL_BLOCK_FVM_DESTROY: {
        return mgr_->FreeSlices(this, 0, mgr_->VSliceMax());
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

typedef struct multi_iotxn_state {
    multi_iotxn_state(size_t total, iotxn_t* txn)
        : txns_completed(0), txns_total(total), status(ZX_OK), original(txn) {}

    fbl::Mutex lock;
    size_t txns_completed TA_GUARDED(lock);
    size_t txns_total TA_GUARDED(lock);
    zx_status_t status TA_GUARDED(lock);
    iotxn_t* original TA_GUARDED(lock);
} multi_iotxn_state_t;

static void multi_iotxn_completion(iotxn_t* txn, void* cookie) {
    multi_iotxn_state_t* state = static_cast<multi_iotxn_state_t*>(cookie);
    bool last_iotxn = false;
    {
        fbl::AutoLock lock(&state->lock);
        state->txns_completed++;
        if (state->status == ZX_OK && txn->status != ZX_OK) {
            state->status = txn->status;
        }
        if (state->txns_completed == state->txns_total) {
            last_iotxn = true;
            iotxn_complete(state->original, state->status, state->original->length);
        }
    }

    if (last_iotxn) {
        delete state;
    }
    iotxn_release(txn);
}

void VPartition::DdkIotxnQueue(iotxn_t* txn) {
    if (txn->offset % BlockSize()) {
        iotxn_complete(txn, ZX_ERR_INVALID_ARGS, 0);
        return;
    }
    // transactions from read()/write() may be truncated
    txn->length = ROUNDDOWN(txn->length, BlockSize());
    if (txn->length == 0) {
        iotxn_complete(txn, ZX_OK, 0);
        return;
    }

    const size_t disk_size = mgr_->DiskSize();
    const size_t slice_size = mgr_->SliceSize();
    size_t vslice_start = txn->offset / slice_size;
    size_t vslice_end = (txn->offset + txn->length - 1) / slice_size;

    fbl::AutoLock lock(&lock_);
    if (vslice_start == vslice_end) {
        // Common case: iotxn occurs within one slice
        uint32_t pslice = SliceGetLocked(vslice_start);
        if (pslice == FVM_SLICE_FREE) {
            iotxn_complete(txn, ZX_ERR_OUT_OF_RANGE, 0);
            return;
        }
        txn->offset = SliceStart(disk_size, slice_size, pslice) +
                      (txn->offset % slice_size);
        iotxn_queue(GetParent(), txn);
        return;
    }

    // Less common case: iotxn spans multiple slices

    // First, check that all slices are allocated.
    // If any are missing, then this txn will fail.
    bool contiguous = true;
    for (size_t vslice = vslice_start; vslice <= vslice_end; vslice++) {
        if (SliceGetLocked(vslice) == FVM_SLICE_FREE) {
            iotxn_complete(txn, ZX_ERR_OUT_OF_RANGE, 0);
            return;
        }
        if (vslice != vslice_start && SliceGetLocked(vslice - 1) + 1 != SliceGetLocked(vslice)) {
            contiguous = false;
        }
    }

    // Ideal case: slices are contiguous
    if (contiguous) {
        uint32_t pslice = SliceGetLocked(vslice_start);
        txn->offset = SliceStart(disk_size, slice_size, pslice) +
                      (txn->offset % slice_size);
        iotxn_queue(GetParent(), txn);
        return;
    }

    // Harder case: Noncontiguous slices
    constexpr size_t kMaxSlices = 32;
    iotxn_t* txns[kMaxSlices];
    const size_t txn_count = vslice_end - vslice_start + 1;
    if (kMaxSlices < (txn_count)) {
        iotxn_complete(txn, ZX_ERR_OUT_OF_RANGE, 0);
        return;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<multi_iotxn_state_t> state(new (&ac) multi_iotxn_state_t(txn_count, txn));
    if (!ac.check()) {
        iotxn_complete(txn, ZX_ERR_NO_MEMORY, 0);
        return;
    }

    size_t length_remaining = txn->length;
    for (size_t i = 0; i < txn_count; i++) {
        size_t vslice = vslice_start + i;
        uint32_t pslice = SliceGetLocked(vslice);

        uint64_t vmo_offset;
        zx_off_t length;
        if (vslice == vslice_start) {
            length = fbl::round_up(txn->offset + 1, slice_size) - txn->offset;
            vmo_offset = 0;
        } else if (vslice == vslice_end) {
            length = length_remaining;
            vmo_offset = txn->length - length_remaining;
        } else {
            length = slice_size;
            vmo_offset = txns[0]->length + slice_size * (i - 1);
        }
        ZX_DEBUG_ASSERT(length <= slice_size);

        zx_status_t status;
        txns[i] = nullptr;
        if ((status = iotxn_clone_partial(txn, vmo_offset, length, &txns[i])) != ZX_OK) {
            while (i-- > 0) {
                iotxn_release(txns[i]);
            }
            iotxn_complete(txn, status, 0);
            return;
        }
        txns[i]->offset = SliceStart(disk_size, slice_size, pslice);
        if (vslice == vslice_start) {
            txns[i]->offset += (txn->offset % slice_size);
        }
        length_remaining -= txns[i]->length;
        txns[i]->complete_cb = multi_iotxn_completion;
        txns[i]->cookie = state.get();
    }
    ZX_DEBUG_ASSERT(length_remaining == 0);

    for (size_t i = 0; i < txn_count; i++) {
        iotxn_queue(GetParent(), txns[i]);
    }
    state.release();
}

zx_off_t VPartition::DdkGetSize() {
    const zx_off_t size = mgr_->DiskSize() * mgr_->SliceSize();
    if (size / mgr_->SliceSize() != size) {
        return fbl::numeric_limits<zx_off_t>::max();
    }
    return size;
}

void VPartition::DdkUnbind() {
    device_remove(zxdev());
}

void VPartition::DdkRelease() {
    delete this;
}

// Block Protocol (VPartition)

void VPartition::BlockSetCallbacks(block_callbacks_t* cb) {
    callbacks_ = cb;
}

void VPartition::BlockGetInfo(block_info_t* info) {
    fbl::AutoLock lock(&lock_);
    *info = info_;
}

void VPartition::BlockRead(zx_handle_t vmo, uint64_t length,
                           uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    Txn(IOTXN_OP_READ, vmo, length, vmo_offset, dev_offset, cookie);
}

void VPartition::BlockWrite(zx_handle_t vmo, uint64_t length, uint64_t vmo_offset,
                            uint64_t dev_offset, void* cookie) {
    Txn(IOTXN_OP_WRITE, vmo, length, vmo_offset, dev_offset, cookie);
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

    // Read vpartition table asynchronously
    status = thrd_create_with_name(&vpm->init_, fvm_load_thread, vpm.get(), "fvm-init");
    if (status < 0) {
        return status;
    }
    vpm.release();
    return ZX_OK;
}
