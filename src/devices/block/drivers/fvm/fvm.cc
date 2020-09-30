// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <utility>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/block.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fvm/fvm.h>

#include "fvm-private.h"
#include "fvm/format.h"
#include "slice-extent.h"
#include "src/lib/uuid/uuid.h"
#include "vpartition.h"

namespace fvm {
namespace {

zx_status_t FvmLoadThread(void* arg) {
  return reinterpret_cast<fvm::VPartitionManager*>(arg)->Load();
}

}  // namespace

VPartitionManager::VPartitionManager(zx_device_t* parent, const block_info_t& info,
                                     size_t block_op_size, const block_impl_protocol_t* bp)
    : ManagerDeviceType(parent),
      info_(info),
      pslice_allocated_count_(0),
      block_op_size_(block_op_size) {
  memcpy(&bp_, bp, sizeof(*bp));
}

VPartitionManager::~VPartitionManager() = default;

// static
zx_status_t VPartitionManager::Bind(void* /*unused*/, zx_device_t* dev) {
  block_info_t block_info;
  block_impl_protocol_t bp;
  size_t block_op_size = 0;
  if (device_get_protocol(dev, ZX_PROTOCOL_BLOCK, &bp) != ZX_OK) {
    zxlogf(ERROR, "block device '%s': does not support block protocol", device_get_name(dev));
    return ZX_ERR_NOT_SUPPORTED;
  }
  bp.ops->query(bp.ctx, &block_info, &block_op_size);

  auto vpm = std::make_unique<VPartitionManager>(dev, block_info, block_op_size, &bp);

  zx_status_t status = vpm->DdkAdd("fvm");
  if (status != ZX_OK) {
    zxlogf(ERROR, "block device '%s': failed to DdkAdd: %s", device_get_name(dev),
           zx_status_get_string(status));
    return status;
  }
  // The VPartitionManager object is owned by the DDK, now that it has been
  // added. It will be deleted when the device is released.
  __UNUSED auto ptr = vpm.release();
  return ZX_OK;
}

void VPartitionManager::DdkInit(ddk::InitTxn txn) {
  init_txn_ = std::move(txn);

  // Read vpartition table asynchronously.
  int rc = thrd_create_with_name(&initialization_thread_, FvmLoadThread, this, "fvm-init");
  if (rc < 0) {
    zxlogf(ERROR, "block device '%s': Could not load initialization thread",
           device_get_name(parent()));
    sync_completion_signal(&worker_completed_);
    // This will schedule the device to be unbound.
    return init_txn_->Reply(ZX_ERR_NO_MEMORY);
  }
  // The initialization thread will reply to |init_txn_| once it is ready to make the
  // device visible and able to be unbound.
}

zx_status_t VPartitionManager::AddPartition(std::unique_ptr<VPartition> vp) const {
  const std::string name = GetAllocatedVPartEntry(vp->GetEntryIndex())->name() + "-p-" +
                           std::to_string(vp->GetEntryIndex());

  zx_status_t status;
  if ((status = vp->DdkAdd(name.c_str())) != ZX_OK) {
    return status;
  }

  // The VPartition object was added to the DDK and is now owned by it. It will be deleted when the
  // device is released.
  __UNUSED auto ptr = vp.release();
  return ZX_OK;
}

struct VpmIoCookie {
  std::atomic<size_t> num_txns;
  std::atomic<zx_status_t> status;
  sync_completion_t signal;
};

static void IoCallback(void* cookie, zx_status_t status, block_op_t* op) {
  VpmIoCookie* c = reinterpret_cast<VpmIoCookie*>(cookie);
  if (status != ZX_OK) {
    c->status.store(status);
  }
  if (c->num_txns.fetch_sub(1) - 1 == 0) {
    sync_completion_signal(&c->signal);
  }
}

zx_status_t VPartitionManager::DoIoLocked(zx_handle_t vmo, size_t off, size_t len,
                                          uint32_t command) const {
  const size_t block_size = info_.block_size;
  const size_t max_transfer = info_.max_transfer_size / block_size;
  size_t len_remaining = len / block_size;
  size_t vmo_offset = 0;
  size_t dev_offset = off / block_size;
  const size_t num_data_txns = fbl::round_up(len_remaining, max_transfer) / max_transfer;

  // Add a "FLUSH" operation to write requests.
  const bool flushing = command == BLOCK_OP_WRITE;
  const size_t num_txns = num_data_txns + (flushing ? 1 : 0);

  fbl::Array<uint8_t> buffer(new uint8_t[block_op_size_ * num_txns], block_op_size_ * num_txns);

  VpmIoCookie cookie;
  cookie.num_txns.store(num_txns);
  cookie.status.store(ZX_OK);
  sync_completion_reset(&cookie.signal);

  for (size_t i = 0; i < num_data_txns; i++) {
    size_t length = std::min(len_remaining, max_transfer);
    len_remaining -= length;

    block_op_t* bop = reinterpret_cast<block_op_t*>(buffer.data() + (block_op_size_ * i));

    bop->command = command;
    bop->rw.vmo = vmo;
    bop->rw.length = static_cast<uint32_t>(length);
    bop->rw.offset_dev = dev_offset;
    bop->rw.offset_vmo = vmo_offset;
    memset(buffer.data() + (block_op_size_ * i) + sizeof(block_op_t), 0,
           block_op_size_ - sizeof(block_op_t));
    vmo_offset += length;
    dev_offset += length;

    Queue(bop, IoCallback, &cookie);
  }

  if (flushing) {
    block_op_t* bop =
        reinterpret_cast<block_op_t*>(buffer.data() + (block_op_size_ * num_data_txns));
    memset(bop, 0, sizeof(*bop));
    bop->command = BLOCKIO_FLUSH;
    Queue(bop, IoCallback, &cookie);
  }

  ZX_DEBUG_ASSERT(len_remaining == 0);
  sync_completion_wait(&cookie.signal, ZX_TIME_INFINITE);
  return static_cast<zx_status_t>(cookie.status.load());
}

zx_status_t VPartitionManager::Load() {
  fbl::AutoLock lock(&lock_);

  ZX_DEBUG_ASSERT(init_txn_.has_value());

  // Let DdkRelease know the thread was successfully created. It is guaranteed
  // that DdkRelease will not be run until after we reply to |init_txn_|.
  initialization_thread_started_ = true;

  // Signal all threads blocked on this thread completion. Join Only happens in DdkRelease, but we
  // need to block earlier to avoid races between DdkRemove and any API call.
  auto singal_completion =
      fbl::MakeAutoCall([this]() { sync_completion_signal(&worker_completed_); });

  auto auto_detach = fbl::MakeAutoCall([&]() TA_NO_THREAD_SAFETY_ANALYSIS {
    zxlogf(ERROR, "Aborting Driver Load");
    // This will schedule the device to be unbound.
    init_txn_->Reply(ZX_ERR_INTERNAL);
  });

  zx::vmo vmo;
  zx_status_t status;
  if ((status = zx::vmo::create(fvm::kBlockSize, 0, &vmo)) != ZX_OK) {
    return status;
  }

  // Read the superblock first, to determine the slice sice
  if ((status = DoIoLocked(vmo.get(), 0, fvm::kBlockSize, BLOCK_OP_READ)) != ZX_OK) {
    zxlogf(ERROR, "Failed to read first block from underlying device");
    return status;
  }

  Header sb;
  status = vmo.read(&sb, 0, sizeof(sb));
  if (status != ZX_OK) {
    return status;
  }

  format_info_ = FormatInfo(sb);

  // Validate the superblock, confirm the slice size
  if ((sb.slice_size * VSliceMax()) / VSliceMax() != sb.slice_size) {
    zxlogf(ERROR, "Slice Size (%zu), VSliceMax (%lu) overflow block address space", sb.slice_size,
           VSliceMax());
    return ZX_ERR_BAD_STATE;
  }
  if (info_.block_size == 0 || SliceSize() % info_.block_size) {
    zxlogf(ERROR, "Bad block (%u) or slice size (%zu)", info_.block_size, SliceSize());
    return ZX_ERR_BAD_STATE;
  }

  // TODO(fxb/40192) Allow the partition table to be different lengths (aligned to blocks):
  //   size_t kMinPartitionTableSize = kBlockSize;
  //   if (sb.vpartition_table_size < kMinPartitionTableSize ||
  //       sb.vpartition_table_size > kMaxPartitionTableByteSize ||
  //       sb.vpartition_table_size % sizeof(VPartitionEntry) != 0) {
  //     zxlogf(ERROR, "Bad vpartition table size %zu (expected %zu-%zu, multiple of %zu)",
  //            sb.vpartition_table_size, kMinPartitionTableSize, kMaxPartitionTableByteSize,
  //            sizeof(VPartitionEntry));
  //     return ZX_ERR_BAD_STATE;
  //
  // Currently the partition table must be a fixed size:
  size_t kPartitionTableLength = PartitionTableLength(kMaxVPartitions);
  if (sb.vpartition_table_size != kPartitionTableLength) {
    zxlogf(ERROR, "Bad vpartition table size %zu (expected %zu)", sb.vpartition_table_size,
           kPartitionTableLength);
    return ZX_ERR_BAD_STATE;
  }

  size_t required_alloc_table_len = AllocTableLengthForUsableSliceCount(sb.pslice_count);
  if (sb.allocation_table_size > kMaxAllocationTableByteSize ||
      sb.allocation_table_size % kBlockSize != 0 ||
      sb.allocation_table_size < required_alloc_table_len) {
    zxlogf(ERROR, "Bad allocation table size %zu (expected at least %zu, multiple of %zu)",
           sb.allocation_table_size, required_alloc_table_len, kBlockSize);
    return ZX_ERR_BAD_STATE;
  }
  if (sb.fvm_partition_size > DiskSize()) {
    zxlogf(ERROR,
           "Block Device too small (fvm_partition_size is %zu and block_device_size is "
           "%zu).",
           sb.fvm_partition_size, DiskSize());
    return ZX_ERR_BAD_STATE;
  }

  // Allocate a buffer big enough for the allocated metadata.
  size_t metadata_vmo_size = sb.GetMetadataAllocatedBytes();

  // Now that the slice size is known, read the rest of the metadata
  auto make_metadata_vmo = [&](size_t offset, fzl::OwnedVmoMapper* out_mapping) {
    fzl::OwnedVmoMapper mapper;
    zx_status_t status = mapper.CreateAndMap(metadata_vmo_size, "fvm-metadata");
    if (status != ZX_OK) {
      return status;
    }

    // Read both copies of metadata, ensure at least one is valid
    if ((status = DoIoLocked(mapper.vmo().get(), offset, metadata_vmo_size, BLOCK_OP_READ)) !=
        ZX_OK) {
      return status;
    }

    *out_mapping = std::move(mapper);
    return ZX_OK;
  };

  fzl::OwnedVmoMapper mapper;
  if ((status = make_metadata_vmo(sb.GetSuperblockOffset(SuperblockType::kPrimary), &mapper)) !=
      ZX_OK) {
    zxlogf(ERROR, "Failed to load metadata vmo: %d", status);
    return status;
  }
  fzl::OwnedVmoMapper mapper_backup;
  if ((status = make_metadata_vmo(sb.GetSuperblockOffset(SuperblockType::kSecondary),
                                  &mapper_backup)) != ZX_OK) {
    zxlogf(ERROR, "Failed to load backup metadata vmo: %d", status);
    return status;
  }

  // Validate metadata headers before growing and pick the correct one.
  std::optional<SuperblockType> use_type =
      ValidateHeader(mapper.start(), mapper_backup.start(), sb.GetMetadataAllocatedBytes());
  if (!use_type) {
    zxlogf(ERROR, "Header validation failure.");
    return ZX_ERR_BAD_STATE;
  }

  if (*use_type == SuperblockType::kPrimary) {
    first_metadata_is_primary_ = true;
    metadata_ = std::move(mapper);
  } else {
    first_metadata_is_primary_ = false;
    metadata_ = std::move(mapper_backup);
  }

  // Whether the metadata should grow or not.
  bool metadata_should_grow =
      GetFvmLocked()->fvm_partition_size < DiskSize() &&
      AllocTableLengthForDiskSize(GetFvmLocked()->fvm_partition_size, GetFvmLocked()->slice_size) <
          GetFvmLocked()->allocation_table_size;

  // Recalculate format info for the valid metadata header.
  format_info_ = FormatInfo(*GetFvmLocked());
  if (metadata_should_grow) {
    size_t new_slice_count = format_info_.GetMaxAddressableSlices(DiskSize());
    size_t target_partition_size =
        format_info_.GetSliceStart(1) + new_slice_count * format_info_.slice_size();
    GetFvmLocked()->fvm_partition_size = target_partition_size;
    GetFvmLocked()->pslice_count = new_slice_count;
    format_info_ = FormatInfo(*GetFvmLocked());

    // Persist the growth.
    if ((status = WriteFvmLocked()) != ZX_OK) {
      zxlogf(ERROR, "Persisting updated header failed.");
      return status;
    }
  }

  // Begin initializing the underlying partitions

  // This will make the device visible and able to be unbound.
  init_txn_->Reply(ZX_OK);
  auto_detach.cancel();

  // 0th vpartition is invalid
  std::unique_ptr<VPartition> vpartitions[fvm::kMaxVPartitions] = {};
  bool has_updated_partitions = false;

  // Iterate through FVM Entry table, allocating the VPartitions which
  // claim to have slices.
  for (size_t i = 1; i < fvm::kMaxVPartitions; i++) {
    auto* entry = GetVPartEntryLocked(i);
    if (entry->slices == 0) {
      continue;
    }

    // Update instance placeholder GUIDs to a newly generated guid.
    if (memcmp(entry->guid, kPlaceHolderInstanceGuid.data(), kGuidSize) == 0) {
      uuid::Uuid uuid = uuid::Uuid::Generate();
      memcpy(entry->guid, uuid.bytes(), uuid::kUuidSize);
      has_updated_partitions = true;
    }

    if ((status = VPartition::Create(this, i, &vpartitions[i])) != ZX_OK) {
      zxlogf(ERROR, "Failed to Create vpartition %zu", i);
      return status;
    }
  }

  if (has_updated_partitions) {
    if ((status = WriteFvmLocked()) != ZX_OK) {
      return status;
    }
  }

  // Iterate through the Slice Allocation table, filling the slice maps
  // of VPartitions.
  for (uint32_t i = 1; i <= GetFvmLocked()->pslice_count; i++) {
    const SliceEntry* entry = GetSliceEntryLocked(i);
    if (entry->IsFree()) {
      continue;
    }
    if (vpartitions[entry->VPartition()] == nullptr) {
      continue;
    }

    // It's fine to load the slices while not holding the vpartition
    // lock; no VPartition devices exist yet.
    vpartitions[entry->VPartition()]->SliceSetUnsafe(entry->VSlice(), i);
    pslice_allocated_count_++;
  }

  lock.release();

  // Iterate through 'valid' VPartitions, and create their devices.
  size_t device_count = 0;
  for (size_t i = 0; i < fvm::kMaxVPartitions; i++) {
    if (vpartitions[i] == nullptr) {
      continue;
    }
    if (GetAllocatedVPartEntry(i)->IsInactive()) {
      zxlogf(ERROR, "Freeing inactive partition");
      FreeSlices(vpartitions[i].get(), 0, VSliceMax());
      continue;
    }
    if ((status = AddPartition(std::move(vpartitions[i]))) != ZX_OK) {
      zxlogf(ERROR, "Failed to add partition: %s", zx_status_get_string(status));
      continue;
    }
    device_count++;
  }

  zxlogf(INFO, "Loaded %lu partitions, slice size=%zu", device_count, format_info_.slice_size());

  return ZX_OK;
}

zx_status_t VPartitionManager::WriteFvmLocked() {
  zx_status_t status;

  GetFvmLocked()->generation++;
  UpdateHash(GetFvmLocked(), format_info_.metadata_size());

  // If we were reading from the primary, write to the backup.
  status = DoIoLocked(metadata_.vmo().get(), BackupOffsetLocked(), format_info_.metadata_size(),
                      BLOCK_OP_WRITE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write metadata");
    return status;
  }

  // We only allow the switch of "write to the other copy of metadata"
  // once a valid version has been written entirely.
  first_metadata_is_primary_ = !first_metadata_is_primary_;
  return ZX_OK;
}

zx_status_t VPartitionManager::FindFreeVPartEntryLocked(size_t* out) const {
  for (size_t i = 1; i < fvm::kMaxVPartitions; i++) {
    const VPartitionEntry* entry = GetVPartEntryLocked(i);
    if (entry->slices == 0) {
      *out = i;
      return ZX_OK;
    }
  }
  return ZX_ERR_NO_SPACE;
}

zx_status_t VPartitionManager::FindFreeSliceLocked(size_t* out, size_t hint) const {
  hint = std::max(hint, 1lu);
  for (size_t i = hint; i <= format_info_.slice_count(); i++) {
    if (GetSliceEntryLocked(i)->IsFree()) {
      *out = i;
      return ZX_OK;
    }
  }
  for (size_t i = 1; i < hint; i++) {
    if (GetSliceEntryLocked(i)->IsFree()) {
      *out = i;
      return ZX_OK;
    }
  }
  return ZX_ERR_NO_SPACE;
}

zx_status_t VPartitionManager::AllocateSlices(VPartition* vp, size_t vslice_start, size_t count) {
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
      if (vp->SliceGetLocked(vslice, &pslice)) {
        zxlogf(ERROR,
               "VPartitionManager::AllocateSlicesLocked: "
               "SliceGetLocked found no physical slice for vslice %zu",
               vslice);
        status = ZX_ERR_INVALID_ARGS;
      }

      // If the vslice is invalid, or there are no more free physical slices, undo all
      // previous allocations.
      if ((status != ZX_OK) || ((status = FindFreeSliceLocked(&pslice, hint)) != ZX_OK)) {
        for (int j = static_cast<int>(i - 1); j >= 0; j--) {
          vslice = vslice_start + j;
          vp->SliceGetLocked(vslice, &pslice);
          FreePhysicalSlice(vp, pslice);
          vp->SliceFreeLocked(vslice);
        }

        return status;
      }

      // Allocate the slice in the partition then mark as allocated.
      vp->SliceSetLocked(vslice, pslice);
      AllocatePhysicalSlice(vp, pslice, vslice);
      hint = pslice + 1;
    }
  }

  if ((status = WriteFvmLocked()) != ZX_OK) {
    // Undo allocation in the event of failure; avoid holding VPartition
    // lock while writing to fvm.
    fbl::AutoLock lock(&vp->lock_);
    for (int j = static_cast<int>(count - 1); j >= 0; j--) {
      auto vslice = vslice_start + j;
      uint64_t pslice;
      // Will always return true, because partition slice allocation is synchronized.
      if (vp->SliceGetLocked(vslice, &pslice)) {
        FreePhysicalSlice(vp, pslice);
        vp->SliceFreeLocked(vslice);
      }
    }
  }

  return status;
}

zx_status_t VPartitionManager::Upgrade(const uint8_t* old_guid, const uint8_t* new_guid) {
  fbl::AutoLock lock(&lock_);
  size_t old_index = 0;
  size_t new_index = 0;

  if (!memcmp(old_guid, new_guid, BLOCK_GUID_LEN)) {
    old_guid = nullptr;
  }

  for (size_t i = 1; i < fvm::kMaxVPartitions; i++) {
    auto entry = GetVPartEntryLocked(i);
    if (entry->slices != 0) {
      if (old_guid && entry->IsActive() && !memcmp(entry->guid, old_guid, BLOCK_GUID_LEN)) {
        old_index = i;
      } else if (entry->IsInactive() && !memcmp(entry->guid, new_guid, BLOCK_GUID_LEN)) {
        new_index = i;
      }
    }
  }

  if (!new_index) {
    return ZX_ERR_NOT_FOUND;
  }

  if (old_index) {
    GetVPartEntryLocked(old_index)->SetActive(false);
  }
  GetVPartEntryLocked(new_index)->SetActive(true);

  return WriteFvmLocked();
}

zx_status_t VPartitionManager::FreeSlices(VPartition* vp, size_t vslice_start, size_t count) {
  fbl::AutoLock lock(&lock_);
  return FreeSlicesLocked(vp, static_cast<uint64_t>(vslice_start), count);
}

zx_status_t VPartitionManager::FreeSlicesLocked(VPartition* vp, uint64_t vslice_start,
                                                size_t count) {
  if (vslice_start + count > VSliceMax() || count > VSliceMax()) {
    return ZX_ERR_INVALID_ARGS;
  }

  bool valid_range = false;
  {
    fbl::AutoLock lock(&vp->lock_);
    if (vp->IsKilledLocked())
      return ZX_ERR_BAD_STATE;

    if (vslice_start == 0) {
      // Special case: Freeing entire VPartition
      for (auto extent = vp->ExtentBegin(); extent.IsValid(); extent = vp->ExtentBegin()) {
        for (size_t i = extent->start(); i < extent->end(); i++) {
          uint64_t pslice;
          vp->SliceGetLocked(i, &pslice);
          FreePhysicalSlice(vp, pslice);
        }
        vp->ExtentDestroyLocked(extent->start());
      }

      // Remove device, VPartition if this was a request to release all slices.
      if (vp->zxdev()) {
        vp->DdkAsyncRemove();
      }
      auto entry = GetVPartEntryLocked(vp->GetEntryIndex());
      entry->Release();
      vp->KillLocked();
      valid_range = true;
    } else {
      for (int i = static_cast<int>(count - 1); i >= 0; i--) {
        auto vslice = vslice_start + i;
        if (vp->SliceCanFree(vslice)) {
          uint64_t pslice;
          vp->SliceGetLocked(vslice, &pslice);
          vp->SliceFreeLocked(vslice);
          FreePhysicalSlice(vp, pslice);
          valid_range = true;
        }
      }
    }
  }

  if (!valid_range) {
    return ZX_ERR_INVALID_ARGS;
  }

  return WriteFvmLocked();
}

void VPartitionManager::Query(volume_info_t* info) {
  info->slice_size = SliceSize();
  info->vslice_count = VSliceMax();
  {
    fbl::AutoLock lock(&lock_);
    info->pslice_total_count = format_info_.slice_count();
    info->pslice_allocated_count = pslice_allocated_count_;
  }
}

void VPartitionManager::FreePhysicalSlice(VPartition* vp, uint64_t pslice) {
  auto entry = GetSliceEntryLocked(pslice);
  ZX_DEBUG_ASSERT_MSG(entry->IsAllocated(), "Freeing already-free slice");
  entry->Release();
  GetVPartEntryLocked(vp->GetEntryIndex())->slices--;
  pslice_allocated_count_--;
}

void VPartitionManager::AllocatePhysicalSlice(VPartition* vp, uint64_t pslice, uint64_t vslice) {
  uint64_t vpart = vp->GetEntryIndex();
  ZX_DEBUG_ASSERT(vpart <= fvm::kMaxVPartitions);
  ZX_DEBUG_ASSERT(vslice <= fvm::kMaxVSlices);
  auto entry = GetSliceEntryLocked(pslice);
  ZX_DEBUG_ASSERT_MSG(entry->IsFree(), "Allocating previously allocated slice");
  entry->Set(vpart, vslice);
  GetVPartEntryLocked(vpart)->slices++;
  pslice_allocated_count_++;
}

SliceEntry* VPartitionManager::GetSliceEntryLocked(size_t index) const {
  ZX_DEBUG_ASSERT(index >= 1);
  Header* header = GetFvmLocked();

  uintptr_t metadata_start = reinterpret_cast<uintptr_t>(header);
  uintptr_t offset = static_cast<uintptr_t>(header->GetSliceEntryOffset(index));

  ZX_DEBUG_ASSERT(header->GetAllocationTableOffset() <= offset);
  ZX_DEBUG_ASSERT(header->GetAllocationTableOffset() + header->GetAllocationTableUsedByteSize() >
                  offset);
  return reinterpret_cast<SliceEntry*>(metadata_start + offset);
}

VPartitionEntry* VPartitionManager::GetVPartEntryLocked(size_t index) const {
  ZX_DEBUG_ASSERT(index >= 1);
  Header* header = GetFvmLocked();

  uintptr_t metadata_start = reinterpret_cast<uintptr_t>(header);
  uintptr_t offset = static_cast<uintptr_t>(header->GetPartitionEntryOffset(index));

  ZX_DEBUG_ASSERT(header->GetPartitionTableOffset() <= offset);
  ZX_DEBUG_ASSERT(header->GetPartitionEntryOffset(index) + header->GetPartitionTableByteSize() >
                  offset);
  return reinterpret_cast<VPartitionEntry*>(metadata_start + offset);
}

// Device protocol (FVM)

zx_status_t VPartitionManager::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_block_volume_VolumeManager_dispatch(this, txn, msg, Ops());
}

zx_status_t VPartitionManager::FIDLAllocatePartition(
    uint64_t slice_count, const fuchsia_hardware_block_partition_GUID* type,
    const fuchsia_hardware_block_partition_GUID* instance, const char* name_data, size_t name_size,
    uint32_t flags, fidl_txn_t* txn) {
  const auto reply = fuchsia_hardware_block_volume_VolumeManagerAllocatePartition_reply;

  constexpr size_t max_name_len =
      std::min<size_t>(fuchsia_hardware_block_partition_NAME_LENGTH, kMaxVPartitionNameLength);
  if (slice_count >= std::numeric_limits<uint32_t>::max()) {
    return reply(txn, ZX_ERR_OUT_OF_RANGE);
  }
  if (slice_count == 0) {
    return reply(txn, ZX_ERR_OUT_OF_RANGE);
  }
  if (name_size > max_name_len) {
    return reply(txn, ZX_ERR_INVALID_ARGS);
  }

  std::string_view name(name_data, name_size);

  // Check that name does not have any NULL terminators in it.
  if (name.find('\0') != std::string::npos) {
    return reply(txn, ZX_ERR_INVALID_ARGS);
  }

  zx_status_t status;
  std::unique_ptr<VPartition> vpart;
  {
    fbl::AutoLock lock(&lock_);
    size_t vpart_entry;
    if ((status = FindFreeVPartEntryLocked(&vpart_entry)) != ZX_OK) {
      return reply(txn, status);
    }

    if ((status = VPartition::Create(this, vpart_entry, &vpart)) != ZX_OK) {
      return reply(txn, status);
    }

    auto* entry = GetVPartEntryLocked(vpart_entry);
    *entry = VPartitionEntry::Create(type->value, instance->value, 0, VPartitionEntry::Name(name),
                                     flags);

    if ((status = AllocateSlicesLocked(vpart.get(), 0, slice_count)) != ZX_OK) {
      entry->slices = 0;  // Undo VPartition allocation
      return reply(txn, status);
    }
  }
  if ((status = AddPartition(std::move(vpart))) != ZX_OK) {
    return reply(txn, status);
  }

  return reply(txn, ZX_OK);
}

zx_status_t VPartitionManager::FIDLQuery(fidl_txn_t* txn) {
  fuchsia_hardware_block_volume_VolumeInfo info;
  Query(&info);
  return fuchsia_hardware_block_volume_VolumeManagerQuery_reply(txn, ZX_OK, &info);
}

zx_status_t VPartitionManager::FIDLGetInfo(fidl_txn_t* txn) const {
  fuchsia_hardware_block_volume_VolumeManagerInfo info;
  info.slice_size = format_info().slice_size();
  info.current_slice_count =
      format_info().GetMaxAddressableSlices(info_.block_size * info_.block_count);
  info.maximum_slice_count = format_info().GetMaxAllocatableSlices();
  return fuchsia_hardware_block_volume_VolumeManagerGetInfo_reply(txn, ZX_OK, &info);
}

zx_status_t VPartitionManager::FIDLActivate(const fuchsia_hardware_block_partition_GUID* old_guid,
                                            const fuchsia_hardware_block_partition_GUID* new_guid,
                                            fidl_txn_t* txn) {
  zx_status_t status = Upgrade(old_guid->value, new_guid->value);
  return fuchsia_hardware_block_volume_VolumeManagerActivate_reply(txn, status);
}

void VPartitionManager::DdkUnbind(ddk::UnbindTxn txn) {
  // Wait untill all work has been completed, before removing the device.
  sync_completion_wait(&worker_completed_, zx::duration::infinite().get());

  txn.Reply();
}

void VPartitionManager::DdkRelease() {
  if (initialization_thread_started_) {
    // Wait until the worker thread exits before freeing the resources.
    thrd_join(initialization_thread_, nullptr);
  }
  delete this;
}

zx_driver_ops_t driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = VPartitionManager::Bind,
};

}  // namespace fvm

// clang-format off
ZIRCON_DRIVER_BEGIN(fvm, fvm::driver_ops, "zircon", "0.1", 2)
  BI_ABORT_IF_AUTOBIND,
  BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_BLOCK),
ZIRCON_DRIVER_END(fvm)
