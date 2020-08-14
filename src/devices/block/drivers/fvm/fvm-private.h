// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_FVM_FVM_PRIVATE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_FVM_FVM_PRIVATE_H_

#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/vmo.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

#include <atomic>
#include <memory>
#include <optional>

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/block.h>
#include <ddktl/protocol/block/partition.h>
#include <fbl/algorithm.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>
#include <fvm/fvm.h>

#include "slice-extent.h"
#include "vpartition.h"

namespace fvm {

using volume_info_t = fuchsia_hardware_block_volume_VolumeInfo;

// Forward declaration
class VPartitionManager;
using ManagerDeviceType =
    ddk::Device<VPartitionManager, ddk::Initializable, ddk::Messageable, ddk::UnbindableNew>;

class VPartitionManager : public ManagerDeviceType {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(VPartitionManager);
  static zx_status_t Bind(void*, zx_device_t* dev);

  // Read the underlying block device, initialize the recorded VPartitions.
  zx_status_t Load();

  // Block Protocol
  size_t BlockOpSize() const { return block_op_size_; }
  void Queue(block_op_t* txn, block_impl_queue_callback completion_cb, void* cookie) const {
    bp_.ops->queue(bp_.ctx, txn, completion_cb, cookie);
  }

  // Acquire access to a VPart Entry which has already been modified (and
  // will, as a consequence, not be de-allocated underneath us).
  vpart_entry_t* GetAllocatedVPartEntry(size_t index) const TA_NO_THREAD_SAFETY_ANALYSIS {
    auto entry = GetVPartEntryLocked(index);
    ZX_ASSERT(entry->slices > 0);
    return entry;
  }

  // Allocate 'count' slices, write back the FVM.
  zx_status_t AllocateSlices(VPartition* vp, size_t vslice_start, size_t count) TA_EXCL(lock_);

  // Deallocate 'count' slices, write back the FVM.
  // If a request is made to remove vslice_count = 0, deallocates the entire
  // VPartition.
  zx_status_t FreeSlices(VPartition* vp, size_t vslice_start, size_t count) TA_EXCL(lock_);

  // Returns global information about the FVM.
  void Query(volume_info_t* info) TA_EXCL(lock_);

  size_t DiskSize() const { return info_.block_count * info_.block_size; }
  size_t SliceSize() const { return format_info_.slice_size(); }
  // format_info_ is calculated on Load and never updated again.
  const FormatInfo& format_info() const { return format_info_; }
  uint64_t VSliceMax() const { return fvm::kMaxVSlices; }
  const block_info_t& Info() const { return info_; }

  void DdkInit(ddk::InitTxn txn);
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();

  void SetFormatInfoForTest(const fvm::FormatInfo& format_info) { format_info_ = format_info; }

  VPartitionManager(zx_device_t* dev, const block_info_t& info, size_t block_op_size,
                    const block_impl_protocol_t* bp);
  ~VPartitionManager();

 private:
  static const fuchsia_hardware_block_volume_VolumeManager_ops* Ops() {
    using Binder = fidl::Binder<VPartitionManager>;
    static const fuchsia_hardware_block_volume_VolumeManager_ops kOps = {
        .AllocatePartition = Binder::BindMember<&VPartitionManager::FIDLAllocatePartition>,
        .Query = Binder::BindMember<&VPartitionManager::FIDLQuery>,
        .GetInfo = Binder::BindMember<&VPartitionManager::FIDLGetInfo>,
        .Activate = Binder::BindMember<&VPartitionManager::FIDLActivate>,
    };
    return &kOps;
  }

  // FIDL interface VolumeManager
  zx_status_t FIDLAllocatePartition(uint64_t slice_count,
                                    const fuchsia_hardware_block_partition_GUID* type,
                                    const fuchsia_hardware_block_partition_GUID* instance,
                                    const char* name_data, size_t name_size, uint32_t flags,
                                    fidl_txn_t* txn);
  zx_status_t FIDLQuery(fidl_txn_t* txn);
  zx_status_t FIDLGetInfo(fidl_txn_t* txn);
  zx_status_t FIDLActivate(const fuchsia_hardware_block_partition_GUID* old_guid,
                           const fuchsia_hardware_block_partition_GUID* new_guid, fidl_txn_t* txn);

  // Marks the partition with instance GUID |old_guid| as inactive,
  // and marks partitions with instance GUID |new_guid| as active.
  //
  // If a partition with |old_guid| does not exist, it is ignored.
  // If |old_guid| equals |new_guid|, then |old_guid| is ignored.
  // If a partition with |new_guid| does not exist, |ZX_ERR_NOT_FOUND|
  // is returned.
  //
  // Updates the FVM metadata atomically.
  zx_status_t Upgrade(const uint8_t* old_guid, const uint8_t* new_guid) TA_EXCL(lock_);

  // Given a VPartition object, add a corresponding ddk device.
  zx_status_t AddPartition(std::unique_ptr<VPartition> vp) const;

  // Update, hash, and write back the current copy of the FVM metadata.
  // Automatically handles alternating writes to primary / backup copy of FVM.
  zx_status_t WriteFvmLocked() TA_REQ(lock_);

  zx_status_t AllocateSlicesLocked(VPartition* vp, size_t vslice_start, size_t count) TA_REQ(lock_);

  zx_status_t FreeSlicesLocked(VPartition* vp, size_t vslice_start, size_t count) TA_REQ(lock_);

  zx_status_t FindFreeVPartEntryLocked(size_t* out) const TA_REQ(lock_);
  zx_status_t FindFreeSliceLocked(size_t* out, size_t hint) const TA_REQ(lock_);

  Header* GetFvmLocked() const TA_REQ(lock_) {
    return reinterpret_cast<Header*>(metadata_.start());
  }

  // Mark a slice as free in the metadata structure.
  // Update free slice accounting.
  void FreePhysicalSlice(VPartition* vp, size_t pslice) TA_REQ(lock_);

  // Mark a slice as allocated in the metadata structure.
  // Update allocated slice accounting.
  void AllocatePhysicalSlice(VPartition* vp, size_t pslice, uint64_t vslice) TA_REQ(lock_);

  // Given a physical slice (acting as an index into the slice table),
  // return the associated slice entry.
  slice_entry_t* GetSliceEntryLocked(size_t index) const TA_REQ(lock_);

  // Given an index into the vpartition table, return the associated
  // virtual partition entry.
  vpart_entry_t* GetVPartEntryLocked(size_t index) const TA_REQ(lock_);

  size_t PrimaryOffsetLocked() const TA_REQ(lock_) {
    return format_info_.GetSuperblockOffset(
        (first_metadata_is_primary_) ? SuperblockType::kPrimary : SuperblockType::kSecondary);
  }

  size_t BackupOffsetLocked() const TA_REQ(lock_) {
    return format_info_.GetSuperblockOffset(
        (first_metadata_is_primary_) ? SuperblockType::kSecondary : SuperblockType::kPrimary);
  }

  zx_status_t DoIoLocked(zx_handle_t vmo, size_t off, size_t len, uint32_t command);

  thrd_t initialization_thread_;
  std::atomic_bool initialization_thread_started_ = false;
  block_info_t info_;  // Cached info from parent device

  fbl::Mutex lock_;
  fzl::OwnedVmoMapper metadata_ TA_GUARDED(lock_);
  bool first_metadata_is_primary_ TA_GUARDED(lock_);
  // Number of currently allocated slices.
  size_t pslice_allocated_count_ TA_GUARDED(lock_);

  // Format information of the fvm. This is only set when the driver is loaded, and not
  // modified.
  fvm::FormatInfo format_info_;

  // Block Protocol
  const size_t block_op_size_;
  block_impl_protocol_t bp_;

  // For replying to the device init hook.
  std::optional<ddk::InitTxn> init_txn_;

  // Worker completion.
  sync_completion_t worker_completed_;
};

}  // namespace fvm

#endif  // SRC_DEVICES_BLOCK_DRIVERS_FVM_FVM_PRIVATE_H_
