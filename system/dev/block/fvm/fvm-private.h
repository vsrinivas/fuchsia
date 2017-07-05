// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <ddk/device.h>
#include <ddk/protocol/block.h>
#include <fvm/fvm.h>
#include <magenta/device/block.h>
#include <magenta/thread_annotations.h>
#include <magenta/types.h>

#ifdef __cplusplus

#include <ddktl/device.h>
#include <ddktl/protocol/block.h>
#include <fs/mapped-vmo.h>
#include <mxtl/algorithm.h>
#include <mxtl/array.h>
#include <mxtl/mutex.h>
#include <mxtl/unique_ptr.h>

namespace fvm {

class VPartitionManager;
using ManagerDeviceType = ddk::Device<VPartitionManager, ddk::Ioctlable, ddk::Unbindable>;

class VPartition;
using PartitionDeviceType = ddk::Device<VPartition, ddk::Ioctlable,
                                        ddk::IotxnQueueable, ddk::GetSizable, ddk::Unbindable>;

class VPartitionManager : public ManagerDeviceType {
public:
    static mx_status_t Create(mx_device_t* dev, mxtl::unique_ptr<VPartitionManager>* out);

    // Read the underlying block device, initialize the recorded VPartitions.
    mx_status_t Load();

    // Given a VPartition object, add a corresponding ddk device.
    mx_status_t AddPartition(mxtl::unique_ptr<VPartition> vp) const;

    // Update, hash, and write back the current copy of the FVM metadata.
    // Automatically handles alternating writes to primary / backup copy of FVM.
    mx_status_t WriteFvmLocked() TA_REQ(lock_);

    // Acquire access to a VPart Entry which has already been modified (and
    // will, as a consequence, not be de-allocated underneath us).
    vpart_entry_t* GetAllocatedVPartEntry(size_t index) const TA_NO_THREAD_SAFETY_ANALYSIS {
        auto entry = GetVPartEntryLocked(index);
        MX_DEBUG_ASSERT(entry->slices > 0);
        return entry;
    }

    slice_entry_t* GetSliceEntryLocked(size_t index) const TA_REQ(lock_) {
        MX_DEBUG_ASSERT(index >= 1);
        uintptr_t metadata_start = reinterpret_cast<uintptr_t>(GetFvmLocked());
        uintptr_t offset = static_cast<uintptr_t>(kAllocTableOffset +
                                                  index * sizeof(slice_entry_t));
        MX_DEBUG_ASSERT(kAllocTableOffset <= offset);
        MX_DEBUG_ASSERT(offset < kAllocTableOffset + AllocTableLength(DiskSize(), SliceSize()));
        return reinterpret_cast<slice_entry_t*>(metadata_start + offset);
    }

    // Allocate 'count' slices, write back the FVM.
    mx_status_t AllocateSlices(VPartition* vp, uint32_t vslice_start, size_t count) TA_EXCL(lock_);
    mx_status_t AllocateSlicesLocked(VPartition* vp, uint32_t vslice_start,
                                     size_t count) TA_REQ(lock_);

    // Deallocate 'count' slices, write back the FVM.
    // If a request is made to remove vslice_count = 0, deallocates the entire
    // VPartition.
    mx_status_t FreeSlices(VPartition* vp, uint32_t vslice_start, size_t count) TA_EXCL(lock_);
    mx_status_t FreeSlicesLocked(VPartition* vp, uint32_t vslice_start,
                                 size_t count) TA_REQ(lock_);

    size_t DiskSize() const { return info_.block_count * info_.block_size; }
    size_t SliceSize() const { return slice_size_; }

    // TODO(smklein): Convert the physical --> virtual mapping structure to a
    // non-linear structure to expand this limit and exploit extent-like locality.
    //
    // This size is somewhat arbitrary; it allows for VPartitions which will
    // practically be large enough for usage while limiting memory costs. This may
    // be expanded later without causing on-disk structures to change.
    static constexpr size_t kVPartMaxSize = (4lu << 40);
    size_t VSliceCount() const { return kVPartMaxSize / SliceSize(); }

    mx_status_t DdkIoctl(uint32_t op, const void* cmd, size_t cmdlen,
                         void* reply, size_t max, size_t* out_actual);
    void DdkUnbind();
    void DdkRelease();

    VPartitionManager(mx_device_t* dev, const block_info_t& info);
    ~VPartitionManager();
    block_info_t info_; // Cached info from parent device
    thrd_t init_;

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(VPartitionManager);

    mx_status_t FindFreeVPartEntryLocked(size_t* out) const TA_REQ(lock_);
    mx_status_t FindFreeSliceLocked(size_t* out, size_t hint) const TA_REQ(lock_);

    fvm_t* GetFvmLocked() const TA_REQ(lock_) {
        return reinterpret_cast<fvm_t*>(metadata_->GetData());
    }

    vpart_entry_t* GetVPartEntryLocked(size_t index) const TA_REQ(lock_) {
        MX_DEBUG_ASSERT(index >= 1);
        uintptr_t metadata_start = reinterpret_cast<uintptr_t>(GetFvmLocked());
        uintptr_t offset = static_cast<uintptr_t>(kVPartTableOffset +
                                                  index * sizeof(vpart_entry_t));
        MX_DEBUG_ASSERT(kVPartTableOffset <= offset);
        MX_DEBUG_ASSERT(offset < kVPartTableOffset + kVPartTableLength);
        return reinterpret_cast<vpart_entry_t*>(metadata_start + offset);
    }

    size_t PrimaryOffsetLocked() const TA_REQ(lock_) {
        return first_metadata_is_primary_ ? 0 : MetadataSize();
    }

    size_t BackupOffsetLocked() const TA_REQ(lock_) {
        return first_metadata_is_primary_ ? MetadataSize() : 0;
    }

    size_t MetadataSize() const {
        return metadata_size_;
    }

    mxtl::Mutex lock_;
    mxtl::unique_ptr<MappedVmo> metadata_ TA_GUARDED(lock_);
    bool first_metadata_is_primary_ TA_GUARDED(lock_);
    size_t metadata_size_;
    size_t slice_size_;
};

class VPartition : public PartitionDeviceType, public ddk::BlockProtocol<VPartition> {
public:
    static mx_status_t Create(VPartitionManager* vpm, size_t entry_index,
                              mxtl::unique_ptr<VPartition>* out);
    // Device Protocol
    mx_status_t DdkIoctl(uint32_t op, const void* cmd, size_t cmdlen,
                         void* reply, size_t max, size_t* out_actual);
    void DdkIotxnQueue(iotxn_t* txn);
    mx_off_t DdkGetSize();
    void DdkUnbind();
    void DdkRelease();

    // Block Protocol
    void Txn(uint32_t opcode, mx_handle_t vmo, uint64_t length,
             uint64_t vmo_offset, uint64_t dev_offset, void* cookie);
    void BlockSetCallbacks(block_callbacks_t* cb);
    void BlockGetInfo(block_info_t* info);
    void BlockRead(mx_handle_t vmo, uint64_t length, uint64_t vmo_offset,
                   uint64_t dev_offset, void* cookie);
    void BlockWrite(mx_handle_t vmo, uint64_t length, uint64_t vmo_offset,
                    uint64_t dev_offset, void* cookie);

    uint32_t SliceGetLocked(size_t vslice) TA_REQ(lock_) {
        MX_DEBUG_ASSERT(vslice < mgr_->VSliceCount());
        return slice_map_[vslice];
    }
    void SliceSetUnsafe(size_t vslice, uint32_t pslice) TA_NO_THREAD_SAFETY_ANALYSIS {
        SliceSetLocked(vslice, pslice);
    }
    void SliceSetLocked(size_t vslice, uint32_t pslice) TA_REQ(lock_) {
        MX_DEBUG_ASSERT(vslice < mgr_->VSliceCount());
        MX_DEBUG_ASSERT(slice_map_[vslice] == 0);
        slice_map_[vslice] = pslice;
        AddBlocksLocked((mgr_->SliceSize() / info_.block_size));
    }
    void SliceFreeLocked(size_t vslice) TA_REQ(lock_) {
        MX_DEBUG_ASSERT(vslice < mgr_->VSliceCount());
        MX_DEBUG_ASSERT(slice_map_[vslice] != 0);
        slice_map_[vslice] = 0;
        AddBlocksLocked(-(mgr_->SliceSize() / info_.block_size));
    }

    size_t BlockSize() const TA_NO_THREAD_SAFETY_ANALYSIS {
        return info_.block_size;
    }
    void AddBlocksLocked(ssize_t nblocks) TA_REQ(lock_) {
        info_.block_count += nblocks;
    }

    size_t GetEntryIndex() const { return entry_index_; }

    void KillLocked() TA_REQ(lock_) { entry_index_ = 0; }
    bool IsKilledLocked() TA_REQ(lock_) { return entry_index_ == 0; }

    VPartition(VPartitionManager* vpm, size_t entry_index, mxtl::Array<uint32_t> slice_map);
    ~VPartition();
    mxtl::Mutex lock_;

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(VPartition);

    mx_device_t* GetParent() const { return mgr_->parent(); }

    VPartitionManager* mgr_;
    size_t entry_index_;
    block_callbacks_t* callbacks_;

    // Mapping of virtual slice number (index) to physical slice number (value).
    // Physical slice zero is reserved to mean "unmapped", so a zeroed slice_map
    // indicates that the vpartition is completely unmapped, and uses no
    // physical slices.
    mxtl::Array<uint32_t> slice_map_ TA_GUARDED(lock_);
    block_info_t info_ TA_GUARDED(lock_);
};

} // namespace fvm

#endif // ifdef __cplusplus

__BEGIN_CDECLS

/////////////////// C++-compatibility definitions (Provided to C++ from C)

// Completions don't exist in C++, thanks to an incompatibility of atomics.
// This function allows C++ functions to synchronously execute iotxns using
// C completions.
//
// Modifies "completion_cb" and "cookie" fields of txn; doesn't free or
// allocate any memory.
void iotxn_synchronous_op(mx_device_t* dev, iotxn_t* txn);

/////////////////// C-compatibility definitions (Provided to C from C++)

// Binds FVM driver to a device; loads the VPartition devices asynchronously in
// a background thread.
mx_status_t fvm_bind(mx_device_t* dev);

__END_CDECLS
