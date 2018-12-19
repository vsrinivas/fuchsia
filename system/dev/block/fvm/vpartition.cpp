// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <zircon/assert.h>

#include "fvm-private.h"
#include "vpartition.h"

namespace fvm {

VPartition::VPartition(VPartitionManager* vpm, size_t entry_index, size_t block_op_size)
    : PartitionDeviceType(vpm->zxdev()), mgr_(vpm), entry_index_(entry_index) {

    memcpy(&info_, &mgr_->Info(), sizeof(block_info_t));
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

    *out = std::move(vp);
    return ZX_OK;
}

uint32_t VPartition::SliceGetLocked(size_t vslice) const {
    ZX_DEBUG_ASSERT(vslice < mgr_->VSliceMax());
    auto extent = --slice_map_.upper_bound(vslice);
    if (!extent.IsValid()) {
        return PSLICE_UNALLOCATED;
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
        slice_map_.insert(std::move(new_extent));
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
        slice_map_.insert(std::move(new_extent));
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

static zx_status_t RequestBoundCheck(const extend_request_t* request, size_t vslice_max) {
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

zx_status_t VPartition::DdkIoctl(uint32_t op, const void* cmd, size_t cmdlen, void* reply,
                                 size_t max, size_t* out_actual) {
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
        if (max < sizeof(fvm_info_t)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        fvm_info_t* info = static_cast<fvm_info_t*>(reply);
        mgr_->Query(info);
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
    multi_txn_state(size_t total, block_op_t* txn, block_impl_queue_callback cb, void* cookie)
        : txns_completed(0), txns_total(total), status(ZX_OK), original(txn), completion_cb(cb),
          cookie(cookie) {}

    fbl::Mutex lock;
    size_t txns_completed TA_GUARDED(lock);
    size_t txns_total TA_GUARDED(lock);
    zx_status_t status TA_GUARDED(lock);
    block_op_t* original TA_GUARDED(lock);
    block_impl_queue_callback completion_cb TA_GUARDED(lock);
    void* cookie TA_GUARDED(lock);
} multi_txn_state_t;

static void multi_txn_completion(void* cookie, zx_status_t status, block_op_t* txn) {
    multi_txn_state_t* state = static_cast<multi_txn_state_t*>(cookie);
    bool last_txn = false;
    {
        fbl::AutoLock lock(&state->lock);
        state->txns_completed++;
        if (state->status == ZX_OK && status != ZX_OK) {
            state->status = status;
        }
        if (state->txns_completed == state->txns_total) {
            last_txn = true;
            state->completion_cb(state->cookie, state->status, state->original);
        }
    }

    if (last_txn) {
        delete state;
    }
    delete[] txn;
}

void VPartition::BlockImplQueue(block_op_t* txn, block_impl_queue_callback completion_cb,
                                void* cookie) {
    ZX_DEBUG_ASSERT(mgr_->BlockOpSize() > 0);
    switch (txn->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
        break;
    // Pass-through operations
    case BLOCK_OP_FLUSH:
        mgr_->Queue(txn, completion_cb, cookie);
        return;
    default:
        fprintf(stderr, "[FVM BlockQueue] Unsupported Command: %x\n", txn->command);
        completion_cb(cookie, ZX_ERR_NOT_SUPPORTED, txn);
        return;
    }

    const uint64_t device_capacity = DdkGetSize() / BlockSize();
    if (txn->rw.length == 0) {
        completion_cb(cookie, ZX_ERR_INVALID_ARGS, txn);
        return;
    } else if ((txn->rw.offset_dev >= device_capacity) ||
               (device_capacity - txn->rw.offset_dev < txn->rw.length)) {
        completion_cb(cookie, ZX_ERR_OUT_OF_RANGE, txn);
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
            completion_cb(cookie, ZX_ERR_OUT_OF_RANGE, txn);
            return;
        }
        txn->rw.offset_dev = SliceStart(disk_size, slice_size, pslice) / BlockSize() +
                             (txn->rw.offset_dev % blocks_per_slice);
        mgr_->Queue(txn, completion_cb, cookie);
        return;
    }

    // Less common case: txn spans multiple slices

    // First, check that all slices are allocated.
    // If any are missing, then this txn will fail.
    bool contiguous = true;
    for (size_t vslice = vslice_start; vslice <= vslice_end; vslice++) {
        if (SliceGetLocked(vslice) == PSLICE_UNALLOCATED) {
            completion_cb(cookie, ZX_ERR_OUT_OF_RANGE, txn);
            return;
        }
        if (vslice != vslice_start && SliceGetLocked(vslice - 1) + 1 != SliceGetLocked(vslice)) {
            contiguous = false;
        }
    }

    // Ideal case: slices are contiguous
    if (contiguous) {
        uint32_t pslice = SliceGetLocked(vslice_start);
        txn->rw.offset_dev = SliceStart(disk_size, slice_size, pslice) / BlockSize() +
                             (txn->rw.offset_dev % blocks_per_slice);
        mgr_->Queue(txn, completion_cb, cookie);
        return;
    }

    // Harder case: Noncontiguous slices
    const size_t txn_count = vslice_end - vslice_start + 1;
    fbl::Vector<block_op_t*> txns;
    txns.reserve(txn_count);

    fbl::AllocChecker ac;
    fbl::unique_ptr<multi_txn_state_t> state(
        new (&ac) multi_txn_state_t(txn_count, txn, completion_cb, cookie));
    if (!ac.check()) {
        completion_cb(cookie, ZX_ERR_NO_MEMORY, txn);
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
            completion_cb(cookie, ZX_ERR_NO_MEMORY, txn);
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
    }
    ZX_DEBUG_ASSERT(length_remaining == 0);

    for (size_t i = 0; i < txn_count; i++) {
        mgr_->Queue(txns[i], multi_txn_completion, state.get());
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

void VPartition::BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out) {
    static_assert(fbl::is_same<decltype(info_out), decltype(&info_)>::value, "Info type mismatch");
    memcpy(info_out, &info_, sizeof(info_));
    *block_op_size_out = mgr_->BlockOpSize();
}

zx_device_t* VPartition::GetParent() const {
    return mgr_->parent();
}

} // namespace fvm
