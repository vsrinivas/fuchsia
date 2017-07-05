// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <fs/mapped-vmo.h>
#include <magenta/compiler.h>
#include <magenta/device/block.h>
#include <magenta/syscalls.h>
#include <magenta/thread_annotations.h>
#include <mxtl/auto_call.h>
#include <mxtl/auto_lock.h>
#include <mxtl/limits.h>
#include <mxtl/new.h>
#include <threads.h>

#include "fvm-private.h"

namespace fvm {

VPartitionManager::VPartitionManager(mx_device_t* parent, const block_info_t& info)
    : ManagerDeviceType(parent), info_(info), metadata_(nullptr), metadata_size_(0),
      slice_size_(0) {}

VPartitionManager::~VPartitionManager() = default;

mx_status_t VPartitionManager::Create(mx_device_t* dev, mxtl::unique_ptr<VPartitionManager>* out) {
    block_info_t block_info;
    size_t actual = 0;
    ssize_t rc = device_ioctl(dev, IOCTL_BLOCK_GET_INFO, nullptr, 0, &block_info,
                              sizeof(block_info), &actual);
    if (rc < 0) {
        return static_cast<mx_status_t>(rc);
    } else if (actual != sizeof(block_info)) {
        return MX_ERR_BAD_STATE;
    } else if (block_info.block_size == 0) {
        return MX_ERR_BAD_STATE;
    }

    mxtl::AllocChecker ac;
    auto vpm = mxtl::make_unique_checked<VPartitionManager>(&ac, dev, block_info);
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }
    *out = mxtl::move(vpm);
    return MX_OK;
}

mx_status_t VPartitionManager::AddPartition(mxtl::unique_ptr<VPartition> vp) const {
    auto ename = reinterpret_cast<const char*>(GetAllocatedVPartEntry(vp->GetEntryIndex())->name);
    char name[FVM_NAME_LEN + 32];
    snprintf(name, sizeof(name), "%.*s-p-%zu", FVM_NAME_LEN, ename, vp->GetEntryIndex());

    mx_status_t status;
    if ((status = vp->DdkAdd(name)) != MX_OK) {
        return status;
    }
    vp.release();
    return MX_OK;
}

mx_status_t VPartitionManager::Load() {
    mxtl::AutoLock lock(&lock_);

    auto auto_detach = mxtl::MakeAutoCall([&]() TA_NO_THREAD_SAFETY_ANALYSIS {
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
    mx_status_t status = iotxn_alloc(&txn, IOTXN_ALLOC_POOL, FVM_BLOCK_SIZE);
    if (status != MX_OK) {
        return status;
    }
    txn->opcode = IOTXN_OP_READ;
    txn->offset = 0;
    txn->length = FVM_BLOCK_SIZE;
    iotxn_synchronous_op(parent(), txn);
    if (txn->status != MX_OK) {
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
        return MX_ERR_BAD_STATE;
    } else if (sb.vpartition_table_size != kVPartTableLength) {
        return MX_ERR_BAD_STATE;
    } else if (sb.allocation_table_size != AllocTableLength(DiskSize(), SliceSize())) {
        return MX_ERR_BAD_STATE;
    }

    metadata_size_ = fvm::MetadataSize(DiskSize(), SliceSize());
    // Now that the slice size is known, read the rest of the metadata
    size_t dual_metadata_size = 2 * MetadataSize();
    mxtl::unique_ptr<MappedVmo> mvmo;
    status = MappedVmo::Create(dual_metadata_size, "fvm-meta", &mvmo);
    if (status != MX_OK) {
        return status;
    }

    // Read both copies of metadata, ensure at least one is valid
    status = iotxn_alloc_vmo(&txn, IOTXN_ALLOC_POOL, mvmo->GetVmo(), 0, dual_metadata_size);
    if (status != MX_OK) {
        return status;
    }
    txn->opcode = IOTXN_OP_READ;
    txn->offset = 0;
    txn->length = dual_metadata_size;
    iotxn_synchronous_op(parent(), txn);
    if (txn->status != MX_OK) {
        status = txn->status;
        iotxn_release(txn);
        return status;
    }
    iotxn_release(txn);

    const void* backup = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mvmo->GetData()) +
                                                 MetadataSize());

    const void* metadata;
    if ((status = fvm_validate_header(mvmo->GetData(), backup, MetadataSize(),
                                      &metadata)) != MX_OK) {
        return status;
    }
    // TODO(smklein): Compare the performance of the current reading behavior
    // (read into VMO, map into VMAR, cut VMAR in half) with an alternative
    // which waits to map the VMO by using mx_vmo_read twice beforehand instead.
    first_metadata_is_primary_ = (metadata == mvmo->GetData());
    if ((status = mvmo->Shrink(PrimaryOffsetLocked(), MetadataSize())) != MX_OK) {
        return status;
    }

    // Begin initializing the underlying partitions
    metadata_ = mxtl::move(mvmo);

    if ((status = DdkAdd("fvm")) != MX_OK) {
        return status;
    }
    auto_detach.cancel();

    // 0th vpartition is invalid
    mxtl::unique_ptr<VPartition> vpartitions[FVM_MAX_ENTRIES] = {};

    // Iterate through FVM Entry table, allocating the VPartitions which
    // claim to have slices.
    for (size_t i = 1; i < FVM_MAX_ENTRIES; i++) {
        if (GetVPartEntryLocked(i)->slices == 0) {
            continue;
        } else if ((status = VPartition::Create(this, i, &vpartitions[i])) != MX_OK) {
            return status;
        }
    }

    // Iterate through the Slice Allocation table, filling the slice maps
    // of VPartitions.
    for (uint32_t i = 1; i < GetFvmLocked()->pslice_count; i++) {
        const slice_entry_t* entry = GetSliceEntryLocked(i);
        if (entry->vpart == FVM_SLICE_FREE) {
            continue;
        }
        if (vpartitions[entry->vpart] == nullptr) {
            return MX_ERR_BAD_STATE;
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
        if (AddPartition(mxtl::move(vpartitions[i]))) {
            continue;
        }
        device_count++;
    }

    return MX_OK;
}

mx_status_t VPartitionManager::WriteFvmLocked() {
    iotxn_t* txn = nullptr;

    mx_status_t status = iotxn_alloc_vmo(&txn, IOTXN_ALLOC_POOL,
                                         metadata_->GetVmo(), 0,
                                         MetadataSize());
    if (status != MX_OK) {
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
    if (status != MX_OK) {
        return status;
    }

    // We only allow the switch of "write to the other copy of metadata"
    // once a valid version has been written entirely.
    first_metadata_is_primary_ = !first_metadata_is_primary_;
    return MX_OK;
}

mx_status_t VPartitionManager::FindFreeVPartEntryLocked(size_t* out) const {
    for (size_t i = 1; i < FVM_MAX_ENTRIES; i++) {
        const vpart_entry_t* entry = GetVPartEntryLocked(i);
        if (entry->slices == 0) {
            *out = i;
            return MX_OK;
        }
    }
    return MX_ERR_NO_SPACE;
}

mx_status_t VPartitionManager::FindFreeSliceLocked(size_t* out, size_t hint) const {
    const size_t maxSlices = UsableSlicesCount(DiskSize(), SliceSize());
    hint = mxtl::max(hint, 1lu);
    for (size_t i = hint; i <= maxSlices; i++) {
        if (GetSliceEntryLocked(i)->vpart == 0) {
            *out = i;
            return MX_OK;
        }
    }
    for (size_t i = 1; i < hint; i++) {
        if (GetSliceEntryLocked(i)->vpart == 0) {
            *out = i;
            return MX_OK;
        }
    }
    return MX_ERR_NO_SPACE;
}

mx_status_t VPartitionManager::AllocateSlices(VPartition* vp, uint32_t vslice_start,
                                              size_t count) {
    mxtl::AutoLock lock(&lock_);
    return AllocateSlicesLocked(vp, vslice_start, count);
}

mx_status_t VPartitionManager::AllocateSlicesLocked(VPartition* vp, uint32_t vslice_start,
                                                    size_t count) {
    if (vslice_start + count > VSliceCount()) {
        return MX_ERR_INVALID_ARGS;
    }

    mx_status_t status = MX_OK;
    size_t hint = 0;

    {
        mxtl::AutoLock lock(&vp->lock_);
        if (vp->IsKilledLocked())
            return MX_ERR_BAD_STATE;
        for (size_t i = 0; i < count; i++) {
            size_t pslice;
            auto vslice = static_cast<uint32_t>(vslice_start + i);
            if (vp->SliceGetLocked(vslice) != 0) {
                status = MX_ERR_INVALID_ARGS;
            }
            if ((status != MX_OK) || (status = FindFreeSliceLocked(&pslice, hint)) != MX_OK) {
                for (uint32_t j = 0; j < i; j++) {
                    vslice = vslice_start + j;
                    GetSliceEntryLocked(vp->SliceGetLocked(vslice))->vpart = 0;
                    vp->SliceFreeLocked(vslice);
                }

                return status;
            }
            vp->SliceSetLocked(vslice, static_cast<uint32_t>(pslice));
            slice_entry_t* alloc_entry = GetSliceEntryLocked(pslice);
            alloc_entry->vpart = static_cast<uint32_t>(vp->GetEntryIndex());
            alloc_entry->vslice = vslice;
            hint = pslice + 1;
        }
    }

    if ((status = WriteFvmLocked()) != MX_OK) {
        // Undo allocation in the event of failure; avoid holding VPartition
        // lock while writing to fvm.
        mxtl::AutoLock lock(&vp->lock_);
        for (uint32_t j = 0; j < count; j++) {
            auto vslice = vslice_start + j;
            GetSliceEntryLocked(vp->SliceGetLocked(vslice))->vpart = 0;
            vp->SliceFreeLocked(vslice);
        }
    }
    return status;
}

mx_status_t VPartitionManager::FreeSlices(VPartition* vp, uint32_t vslice_start,
                                          size_t count) {
    mxtl::AutoLock lock(&lock_);
    return FreeSlicesLocked(vp, vslice_start, count);
}

mx_status_t VPartitionManager::FreeSlicesLocked(VPartition* vp, uint32_t vslice_start,
                                                size_t count) {
    if (vslice_start + count > VSliceCount() || count > VSliceCount()) {
        return MX_ERR_INVALID_ARGS;
    } else if (vslice_start == 0) {
        count = VSliceCount();
    }

    bool freed_something = false;
    {
        mxtl::AutoLock lock(&vp->lock_);
        if (vp->IsKilledLocked())
            return MX_ERR_BAD_STATE;

        // Sync first, before removing slices, so iotxns in-flight cannot
        // operate on 'unowned' slices.
        mx_status_t status;
        status = device_ioctl(parent(), IOCTL_DEVICE_SYNC, nullptr, 0, nullptr, 0, nullptr);
        if (status != MX_OK) {
            return status;
        }

        for (size_t i = 0; i < count; i++) {
            auto vslice = vslice_start + i;
            auto pslice = vp->SliceGetLocked(vslice);
            if (pslice != 0) {
                GetSliceEntryLocked(pslice)->vpart = 0;
                vp->SliceFreeLocked(vslice);
                freed_something = true;
            }
        }

        // Remove device, VPartition if this was a request to free all slices.
        if (vslice_start == 0 && freed_something) {
            device_remove(mxdev());
            auto entry = GetVPartEntryLocked(vp->GetEntryIndex());
            entry->clear();
            vp->KillLocked();
        }
    }

    if (!freed_something) {
        return MX_ERR_INVALID_ARGS;
    }
    return WriteFvmLocked();
}

// Device protocol (FVM)

mx_status_t VPartitionManager::DdkIoctl(uint32_t op, const void* cmd,
                                        size_t cmdlen, void* reply, size_t max,
                                        size_t* out_actual) {
    switch (op) {
    case IOCTL_BLOCK_FVM_ALLOC: {
        if (cmdlen < sizeof(alloc_req_t))
            return MX_ERR_BUFFER_TOO_SMALL;
        const alloc_req_t* request = static_cast<const alloc_req_t*>(cmd);

        if (request->slice_count >= mxtl::numeric_limits<uint32_t>::max()) {
            return MX_ERR_OUT_OF_RANGE;
        } else if (request->slice_count == 0) {
            return MX_ERR_OUT_OF_RANGE;
        }

        mx_status_t status;
        mxtl::unique_ptr<VPartition> vpart;
        {
            mxtl::AutoLock lock(&lock_);
            size_t vpart_entry;
            if ((status = FindFreeVPartEntryLocked(&vpart_entry)) != MX_OK) {
                return status;
            }

            if ((status = VPartition::Create(this, vpart_entry, &vpart)) != MX_OK) {
                return status;
            }

            auto entry = GetVPartEntryLocked(vpart_entry);
            entry->init(request->type, request->guid,
                        static_cast<uint32_t>(request->slice_count),
                        request->name);

            if ((status = AllocateSlicesLocked(vpart.get(), 0,
                                               request->slice_count)) != MX_OK) {
                entry->slices = 0; // Undo VPartition allocation
                return status;
            }
        }
        if ((status = AddPartition(mxtl::move(vpart))) != MX_OK) {
            return status;
        }
        return MX_OK;
    }
    case IOCTL_BLOCK_FVM_QUERY: {
        if (max < sizeof(fvm_info_t))
            return MX_ERR_BUFFER_TOO_SMALL;
        fvm_info_t* info = static_cast<fvm_info_t*>(reply);
        info->slice_size = SliceSize();
        info->vslice_count = VSliceCount();
        *out_actual = sizeof(fvm_info_t);
        return MX_OK;
    }
    default:
        return MX_ERR_NOT_SUPPORTED;
    }

    return MX_ERR_NOT_SUPPORTED;
}

void VPartitionManager::DdkUnbind() {
    device_remove(mxdev());
}

void VPartitionManager::DdkRelease() {
    thrd_join(init_, nullptr);
    delete this;
}

VPartition::VPartition(VPartitionManager* vpm, size_t entry_index,
                       mxtl::Array<uint32_t> slice_map)
    : PartitionDeviceType(vpm->mxdev()), mgr_(vpm),
      entry_index_(entry_index), slice_map_(mxtl::move(slice_map)) {

    memcpy(&info_, &mgr_->info_, sizeof(block_info_t));
    info_.block_count = 0;
}

VPartition::~VPartition() = default;

mx_status_t VPartition::Create(VPartitionManager* vpm, size_t entry_index,
                               mxtl::unique_ptr<VPartition>* out) {
    MX_DEBUG_ASSERT(entry_index != 0);

    mxtl::AllocChecker ac;
    mxtl::Array<uint32_t> slice_map(new (&ac) uint32_t[vpm->VSliceCount()](), vpm->VSliceCount());
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }

    auto vp = mxtl::make_unique_checked<VPartition>(&ac, vpm, entry_index, mxtl::move(slice_map));
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }

    *out = mxtl::move(vp);
    return MX_OK;
}

static void vpart_block_complete(iotxn_t* txn, void* cookie) {
    block_callbacks_t* cb;
    memcpy(&cb, txn->extra, sizeof(void*));
    cb->complete(cookie, txn->status);
    iotxn_release(txn);
}

void VPartition::Txn(uint32_t opcode, mx_handle_t vmo, uint64_t length,
                     uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    mx_status_t status;
    iotxn_t* txn;
    if ((status = iotxn_alloc_vmo(&txn, IOTXN_ALLOC_POOL, vmo, vmo_offset,
                                  length)) != MX_OK) {
        callbacks_->complete(cookie, status);
        return;
    }
    txn->opcode = opcode;
    txn->offset = dev_offset;
    txn->complete_cb = vpart_block_complete;
    txn->cookie = cookie;
    memcpy(txn->extra, &callbacks_, sizeof(void*));
    DdkIotxnQueue(txn);
}

// Device protocol (VPartition)

mx_status_t VPartition::DdkIoctl(uint32_t op, const void* cmd, size_t cmdlen,
                                 void* reply, size_t max, size_t* out_actual) {
    switch (op) {
    case IOCTL_BLOCK_GET_INFO: {
        block_info_t* info = static_cast<block_info_t*>(reply);
        if (max < sizeof(*info))
            return MX_ERR_BUFFER_TOO_SMALL;
        mxtl::AutoLock lock(&lock_);
        if (IsKilledLocked())
            return MX_ERR_BAD_STATE;
        memcpy(info, &info_, sizeof(*info));
        *out_actual = sizeof(*info);
        return MX_OK;
    }
    case IOCTL_BLOCK_FVM_QUERY: {
        if (max < sizeof(fvm_info_t))
            return MX_ERR_BUFFER_TOO_SMALL;
        fvm_info_t* info = static_cast<fvm_info_t*>(reply);
        info->slice_size = mgr_->SliceSize();
        info->vslice_count = mgr_->VSliceCount();
        *out_actual = sizeof(fvm_info_t);
        return MX_OK;
    }
    case IOCTL_BLOCK_GET_TYPE_GUID: {
        char* guid = static_cast<char*>(reply);
        if (max < FVM_GUID_LEN)
            return MX_ERR_BUFFER_TOO_SMALL;
        mxtl::AutoLock lock(&lock_);
        if (IsKilledLocked())
            return MX_ERR_BAD_STATE;
        memcpy(guid, mgr_->GetAllocatedVPartEntry(entry_index_)->type, FVM_GUID_LEN);
        *out_actual = FVM_GUID_LEN;
        return MX_OK;
    }
    case IOCTL_BLOCK_GET_PARTITION_GUID: {
        char* guid = static_cast<char*>(reply);
        if (max < FVM_GUID_LEN)
            return MX_ERR_BUFFER_TOO_SMALL;
        mxtl::AutoLock lock(&lock_);
        if (IsKilledLocked())
            return MX_ERR_BAD_STATE;
        memcpy(guid, mgr_->GetAllocatedVPartEntry(entry_index_)->guid, FVM_GUID_LEN);
        *out_actual = FVM_GUID_LEN;
        return MX_OK;
    }
    case IOCTL_BLOCK_GET_NAME: {
        char* name = static_cast<char*>(reply);
        if (max < FVM_NAME_LEN + 1)
            return MX_ERR_BUFFER_TOO_SMALL;
        mxtl::AutoLock lock(&lock_);
        if (IsKilledLocked())
            return MX_ERR_BAD_STATE;
        memcpy(name, mgr_->GetAllocatedVPartEntry(entry_index_)->name, FVM_NAME_LEN);
        name[FVM_NAME_LEN] = 0;
        *out_actual = strlen(name);
        return MX_OK;
    }
    case IOCTL_DEVICE_SYNC: {
        // Propagate sync to parent device
        return device_ioctl(GetParent(), IOCTL_DEVICE_SYNC, nullptr, 0, nullptr, 0, nullptr);
    }
    case IOCTL_BLOCK_FVM_EXTEND: {
        if (cmdlen < sizeof(extend_request_t))
            return MX_ERR_BUFFER_TOO_SMALL;
        const extend_request_t* request = static_cast<const extend_request_t*>(cmd);

        if (request->offset == 0 ||
            request->offset >= mxtl::numeric_limits<uint32_t>::max()) {
            return MX_ERR_OUT_OF_RANGE;
        } else if (request->length == 0) {
            // No-op extend request
            return MX_OK;
        } else if (request->length >= mxtl::numeric_limits<uint32_t>::max()) {
            return MX_ERR_OUT_OF_RANGE;
        } else if (request->length + request->offset >= mxtl::numeric_limits<uint32_t>::max()) {
            return MX_ERR_OUT_OF_RANGE;
        }

        auto vslice_start = static_cast<uint32_t>(request->offset);
        return mgr_->AllocateSlices(this, vslice_start, request->length);
    }
    case IOCTL_BLOCK_FVM_SHRINK: {
        if (cmdlen < sizeof(extend_request_t))
            return MX_ERR_BUFFER_TOO_SMALL;
        const extend_request_t* request = static_cast<const extend_request_t*>(cmd);

        if (request->offset == 0 || request->offset >= mgr_->VSliceCount()) {
            return MX_ERR_OUT_OF_RANGE;
        } else if (request->length == 0) {
            // No-op shrink request
            return MX_OK;
        } else if (request->length >= mgr_->VSliceCount()) {
            return MX_ERR_OUT_OF_RANGE;
        } else if (request->length + request->offset >= mgr_->VSliceCount()) {
            return MX_ERR_OUT_OF_RANGE;
        }

        return mgr_->FreeSlices(this, static_cast<uint32_t>(request->offset),
                                request->length);
    }
    case IOCTL_BLOCK_FVM_DESTROY: {
        return mgr_->FreeSlices(this, 0, mgr_->VSliceCount());
    }
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}

typedef struct multi_iotxn_state {
    multi_iotxn_state(size_t total, iotxn_t* txn)
        : txns_completed(0), txns_total(total), status(MX_OK), original(txn) {}

    mxtl::Mutex lock;
    size_t txns_completed TA_GUARDED(lock);
    size_t txns_total TA_GUARDED(lock);
    mx_status_t status TA_GUARDED(lock);
    iotxn_t* original TA_GUARDED(lock);
} multi_iotxn_state_t;

static void multi_iotxn_completion(iotxn_t* txn, void* cookie) {
    multi_iotxn_state_t* state = static_cast<multi_iotxn_state_t*>(cookie);
    bool last_iotxn = false;
    {
        mxtl::AutoLock lock(&state->lock);
        state->txns_completed++;
        if (state->status == MX_OK && txn->status != MX_OK) {
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
        iotxn_complete(txn, MX_ERR_INVALID_ARGS, 0);
        return;
    }
    // transactions from read()/write() may be truncated
    txn->length = ROUNDDOWN(txn->length, BlockSize());
    if (txn->length == 0) {
        iotxn_complete(txn, MX_OK, 0);
        return;
    }

    const size_t disk_size = mgr_->DiskSize();
    const size_t slice_size = mgr_->SliceSize();
    uint32_t vslice_start = static_cast<uint32_t>(txn->offset / slice_size);
    uint32_t vslice_end = static_cast<uint32_t>((txn->offset + txn->length - 1) / slice_size);

    mxtl::AutoLock lock(&lock_);
    if (vslice_start == vslice_end) {
        // Common case: iotxn occurs within one slice
        uint32_t pslice = SliceGetLocked(vslice_start);
        if (pslice == FVM_SLICE_FREE) {
            iotxn_complete(txn, MX_ERR_OUT_OF_RANGE, 0);
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
    for (uint32_t vslice = vslice_start; vslice <= vslice_end; vslice++) {
        if (SliceGetLocked(vslice) == FVM_SLICE_FREE) {
            iotxn_complete(txn, MX_ERR_OUT_OF_RANGE, 0);
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
        iotxn_complete(txn, MX_ERR_OUT_OF_RANGE, 0);
        return;
    }

    mxtl::AllocChecker ac;
    mxtl::unique_ptr<multi_iotxn_state_t> state(new (&ac) multi_iotxn_state_t(txn_count, txn));
    if (!ac.check()) {
        iotxn_complete(txn, MX_ERR_NO_MEMORY, 0);
        return;
    }

    size_t length_remaining = txn->length;
    for (size_t i = 0; i < txn_count; i++) {
        size_t vslice = vslice_start + i;
        uint32_t pslice = SliceGetLocked(vslice);

        uint64_t vmo_offset;
        mx_off_t length;
        if (vslice == vslice_start) {
            length = mxtl::roundup(txn->offset, slice_size) - txn->offset;
            vmo_offset = 0;
        } else if (vslice == vslice_end) {
            length = length_remaining;
            vmo_offset = txn->length - length_remaining;
        } else {
            length = slice_size;
            vmo_offset = txns[0]->length + slice_size * (i - 1);
        }
        MX_DEBUG_ASSERT(length <= slice_size);

        mx_status_t status;
        txns[i] = nullptr;
        if ((status = iotxn_clone_partial(txn, vmo_offset, length, &txns[i])) != MX_OK) {
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
    MX_DEBUG_ASSERT(length_remaining == 0);

    for (size_t i = 0; i < txn_count; i++) {
        iotxn_queue(GetParent(), txns[i]);
    }
    state.release();
}

mx_off_t VPartition::DdkGetSize() {
    return mgr_->DiskSize();
}

void VPartition::DdkUnbind() {
    device_remove(mxdev());
}

void VPartition::DdkRelease() {
    delete this;
}

// Block Protocol (VPartition)

void VPartition::BlockSetCallbacks(block_callbacks_t* cb) {
    callbacks_ = cb;
}

void VPartition::BlockGetInfo(block_info_t* info) {
    mxtl::AutoLock lock(&lock_);
    *info = info_;
}

void VPartition::BlockRead(mx_handle_t vmo, uint64_t length,
                           uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    Txn(IOTXN_OP_READ, vmo, length, vmo_offset, dev_offset, cookie);
}

void VPartition::BlockWrite(mx_handle_t vmo, uint64_t length, uint64_t vmo_offset,
                            uint64_t dev_offset, void* cookie) {
    Txn(IOTXN_OP_WRITE, vmo, length, vmo_offset, dev_offset, cookie);
}

} // namespace fvm

// C-compatibility definitions

static mx_status_t fvm_load_thread(void* arg) {
    return reinterpret_cast<fvm::VPartitionManager*>(arg)->Load();
}

mx_status_t fvm_bind(mx_device_t* parent) {
    mxtl::unique_ptr<fvm::VPartitionManager> vpm;
    mx_status_t status = fvm::VPartitionManager::Create(parent, &vpm);
    if (status != MX_OK) {
        return status;
    }

    // Read vpartition table asynchronously
    status = thrd_create_with_name(&vpm->init_, fvm_load_thread, vpm.get(), "fvm-init");
    if (status < 0) {
        return status;
    }
    vpm.release();
    return MX_OK;
}
