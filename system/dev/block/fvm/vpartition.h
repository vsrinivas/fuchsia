// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/block.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

#include "slice-extent.h"

namespace fvm {

// Forward Declaration
class VPartitionManager;
class VPartition;

using PartitionDeviceType =
    ddk::Device<VPartition, ddk::Ioctlable, ddk::GetSizable, ddk::Unbindable>;

class VPartition : public PartitionDeviceType, public ddk::BlockImplProtocol<VPartition> {
public:
    using SliceMap = fbl::WAVLTree<size_t, fbl::unique_ptr<SliceExtent>>;

    static zx_status_t Create(VPartitionManager* vpm, size_t entry_index,
                              fbl::unique_ptr<VPartition>* out);
    // Device Protocol
    zx_status_t DdkIoctl(uint32_t op, const void* cmd, size_t cmdlen, void* reply, size_t max,
                         size_t* out_actual);
    zx_off_t DdkGetSize();
    void DdkUnbind();
    void DdkRelease();

    // Block Protocol
    void BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out);
    void BlockImplQueue(block_op_t* txn, block_impl_queue_callback completion_cb, void* cookie);

    SliceMap::iterator ExtentBegin() TA_REQ(lock_) { return slice_map_.begin(); }

    // Given a virtual slice, return the physical slice allocated
    // to it. If no slice is allocated, return PSLICE_UNALLOCATED.
    uint32_t SliceGetLocked(size_t vslice) const TA_REQ(lock_);

    // Check slices starting from |vslice_start|.
    // Sets |*count| to the number of contiguous allocated or unallocated slices found.
    // Sets |*allocated| to true if the vslice range is allocated, and false otherwise.
    zx_status_t CheckSlices(size_t vslice_start, size_t* count, bool* allocated) TA_EXCL(lock_);

    zx_status_t SliceSetUnsafe(size_t vslice, uint32_t pslice) TA_NO_THREAD_SAFETY_ANALYSIS {
        return SliceSetLocked(vslice, pslice);
    }
    zx_status_t SliceSetLocked(size_t vslice, uint32_t pslice) TA_REQ(lock_);

    bool SliceCanFree(size_t vslice) const TA_REQ(lock_) {
        auto extent = --slice_map_.upper_bound(vslice);
        return extent.IsValid() && extent->get(vslice) != PSLICE_UNALLOCATED;
    }

    // Returns "true" if slice freed successfully, false otherwise.
    // If freeing from the back of an extent, guaranteed not to fail.
    bool SliceFreeLocked(size_t vslice) TA_REQ(lock_);

    // Destroy the extent containing the vslice.
    void ExtentDestroyLocked(size_t vslice) TA_REQ(lock_);

    size_t BlockSize() const TA_NO_THREAD_SAFETY_ANALYSIS { return info_.block_size; }
    void AddBlocksLocked(ssize_t nblocks) TA_REQ(lock_) { info_.block_count += nblocks; }

    size_t GetEntryIndex() const { return entry_index_; }

    void KillLocked() TA_REQ(lock_) { entry_index_ = 0; }
    bool IsKilledLocked() TA_REQ(lock_) { return entry_index_ == 0; }

    VPartition(VPartitionManager* vpm, size_t entry_index, size_t block_op_size);
    ~VPartition();
    fbl::Mutex lock_;

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(VPartition);

    zx_device_t* GetParent() const;

    VPartitionManager* mgr_;
    size_t entry_index_;

    // Mapping of virtual slice number (index) to physical slice number (value).
    // Physical slice zero is reserved to mean "unmapped", so a zeroed slice_map
    // indicates that the vpartition is completely unmapped, and uses no
    // physical slices.
    SliceMap slice_map_ TA_GUARDED(lock_);
    block_info_t info_ TA_GUARDED(lock_);
};

} // namespace fvm
