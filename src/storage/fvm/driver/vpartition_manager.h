// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FVM_DRIVER_VPARTITION_MANAGER_H_
#define SRC_STORAGE_FVM_DRIVER_VPARTITION_MANAGER_H_

#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <fuchsia/hardware/block/cpp/banjo.h>
#include <fuchsia/hardware/block/partition/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/result.h>
#include <lib/zx/vmo.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/types.h>

#include <atomic>
#include <memory>
#include <optional>

#include <ddktl/device.h>
#include <fbl/algorithm.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>

#include "src/storage/fvm/driver/diagnostics.h"
#include "src/storage/fvm/driver/slice_extent.h"
#include "src/storage/fvm/driver/vpartition.h"
#include "src/storage/fvm/format.h"
#include "src/storage/fvm/fvm.h"
#include "src/storage/fvm/metadata.h"

namespace fvm {

using fuchsia_hardware_block_volume::wire::VolumeManagerInfo;

// Forward declaration
class VPartitionManager;
using ManagerDeviceType =
    ddk::Device<VPartitionManager, ddk::Initializable,
                ddk::Messageable<fuchsia_hardware_block_volume::VolumeManager>::Mixin,
                ddk::Unbindable, ddk::ChildPreReleaseable>;

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
  VPartitionEntry* GetAllocatedVPartEntry(size_t index) const TA_NO_THREAD_SAFETY_ANALYSIS {
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
  void GetInfoInternal(VolumeManagerInfo* info) TA_EXCL(lock_);

  uint64_t GetPartitionLimitInternal(size_t index) const;
  zx_status_t GetPartitionLimitInternal(const uint8_t* guid, uint64_t* slice_count) const;
  zx_status_t SetPartitionLimitInternal(const uint8_t* guid, uint64_t slice_count);
  zx_status_t SetPartitionNameInternal(const uint8_t* guid, std::string_view name);

  size_t DiskSize() const { return info_.block_count * info_.block_size; }
  size_t slice_size() const { return slice_size_; }
  uint64_t VSliceMax() const { return fvm::kMaxVSlices; }
  const block_info_t& Info() const { return info_; }

  // Returns a copy of the current header. See also GetHeaderLocked for a mutable version of the
  // header from inside the lock.
  fvm::Header GetHeader() const;

  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  void DdkChildPreRelease(void* child);

  VPartitionManager(zx_device_t* parent, const block_info_t& info, size_t block_op_size,
                    const block_impl_protocol_t* bp);
  ~VPartitionManager();

  // Allocates the partition, returning it without adding it to the device manager. Production code
  // will go through the FIDL API, this is exposed separately to allow testing without FIDL.
  zx::result<std::unique_ptr<VPartition>> AllocatePartition(
      uint64_t slice_count, const fuchsia_hardware_block_partition::wire::Guid& type,
      const fuchsia_hardware_block_partition::wire::Guid& instance, fidl::StringView name,
      uint32_t flags);

  // Returns a reference to the Diagnostics that this instance publishes to.
  Diagnostics& diagnostics() { return diagnostics_; }

 private:
  void AllocatePartition(AllocatePartitionRequestView request,
                         AllocatePartitionCompleter::Sync& completer) override;
  void GetInfo(GetInfoCompleter::Sync& completer) override;
  void Activate(ActivateRequestView request, ActivateCompleter::Sync& completer) override;
  void GetPartitionLimit(GetPartitionLimitRequestView request,
                         GetPartitionLimitCompleter::Sync& completer) override;
  void SetPartitionLimit(SetPartitionLimitRequestView request,
                         SetPartitionLimitCompleter::Sync& completer) override;
  void SetPartitionName(SetPartitionNameRequestView request,
                        SetPartitionNameCompleter::Sync& completer) override;

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
  zx_status_t AddPartition(std::unique_ptr<VPartition> vp) TA_EXCL(lock_);

  // Update, hash, and write back the current copy of the FVM metadata.
  // Automatically handles alternating writes to primary / backup copy of FVM.
  zx_status_t WriteFvmLocked() TA_REQ(lock_);

  zx_status_t AllocateSlicesLocked(VPartition* vp, size_t vslice_start, size_t count) TA_REQ(lock_);

  zx_status_t FreeSlicesLocked(VPartition* vp, size_t vslice_start, size_t count) TA_REQ(lock_);

  zx_status_t FindFreeVPartEntryLocked(size_t* out) const TA_REQ(lock_);
  zx_status_t FindFreeSliceLocked(size_t* out, size_t hint) const TA_REQ(lock_);

  // See also GetHeader() for unlocked access.
  Header* GetHeaderLocked() const TA_REQ(lock_) { return &metadata_.GetHeader(); }

  // Mark a slice as free in the metadata structure.
  // Update free slice accounting.
  void FreePhysicalSlice(VPartition* vp, size_t pslice) TA_REQ(lock_);

  // Mark a slice as allocated in the metadata structure.
  // Update allocated slice accounting.
  void AllocatePhysicalSlice(VPartition* vp, size_t pslice, uint64_t vslice) TA_REQ(lock_);

  // Given a physical slice (acting as an index into the slice table),
  // return the associated slice entry.
  SliceEntry* GetSliceEntryLocked(size_t index) const TA_REQ(lock_);

  // Given an index into the vpartition table, return the associated
  // virtual partition entry.
  VPartitionEntry* GetVPartEntryLocked(size_t index) const TA_REQ(lock_);

  // Returns the number of the partition with the given GUID. If there are multiple ones (there
  // should not be), returns the first one. If there are no matches, returns 0 (partitions are
  // 1-indexed).
  size_t GetPartitionNumberLocked(const uint8_t* guid) const TA_REQ(lock_);

  zx_status_t DoIoLocked(zx_handle_t vmo, size_t off, size_t len, uint32_t command) const;

  // Writes the current partition information out to the system log.
  void LogPartitionInfoLocked() const TA_REQ(lock_);

  thrd_t initialization_thread_;
  std::atomic_bool initialization_thread_started_ = false;
  block_info_t info_;  // Cached info from parent device

  mutable fbl::Mutex lock_;
  Metadata metadata_ TA_GUARDED(lock_);
  // Number of currently allocated slices.
  size_t pslice_allocated_count_ TA_GUARDED(lock_) = 0;

  Diagnostics diagnostics_;

  // Set when the driver is loaded and never changed.
  size_t slice_size_ = 0;

  // Stores the maximum size in slices for each partition, 1-indexed (0 elt is not used) the same as
  // GetVPartEntryLocked(). A 0 max size means there is no maximum for this partition.
  //
  // These are 0-initialized and set by the FIDL call SetPartitionLimit. It would be better in the
  // future if this information could be persisted in the partition table. But currently we want
  // to keep the max size without changing the on-disk format. fshost will set these on startup
  // when configured to do so.
  uint64_t max_partition_sizes_[fvm::kMaxVPartitions] TA_GUARDED(lock_) = {0};

  // Keeps track of which FVM entries currently have running devices to prevent duplicate device
  // names. The VPartition devices are named after their partition name and FVM entry index. When a
  // partition is destroyed, the entry in FVM is cleared before the device is removed. If a new
  // partition is created with the same name as a partition that was just destroyed but before the
  // previous partition's device is removed then it will likely get the same FVM entry index and
  // have the same device name. This field is used to prevent reusing an FVM entry for the brief
  // period of time when the entry is clear but the device hasn't been removed yet.
  bool device_bound_at_entry_[fvm::kMaxVPartitions] TA_GUARDED(lock_) = {};

  // Block Protocol
  const size_t block_op_size_;
  block_impl_protocol_t bp_;

  // For replying to the device init hook. Empty when not initialized by the DDK yet and when run
  // in unit tests. To allow for test operation, null check this and ignore the txn if unset.
  std::optional<ddk::InitTxn> init_txn_;

  // Worker completion.
  sync_completion_t worker_completed_;
};

}  // namespace fvm

#endif  // SRC_STORAGE_FVM_DRIVER_VPARTITION_MANAGER_H_
