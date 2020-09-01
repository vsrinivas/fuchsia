// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vpartition.h"

#include <zircon/assert.h>

#include <memory>
#include <type_traits>
#include <utility>

#include <ddk/debug.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/vector.h>

#include "fvm-private.h"

namespace fvm {

namespace {

void SetOperationDeviceOffset(uint64_t offset, block_op_t* txn) {
  switch (txn->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
      txn->rw.offset_dev = offset;
      break;
    case BLOCK_OP_TRIM:
      txn->trim.offset_dev = offset;
      break;
    default:
      ZX_ASSERT_MSG(false, "Unexpected operation type");
  }
}

void SetOperationVmoOffset(uint64_t offset, block_op_t* txn) {
  switch (txn->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
      txn->rw.offset_vmo = offset;
      break;
    case BLOCK_OP_TRIM:
      break;
    default:
      ZX_ASSERT_MSG(false, "Unexpected operation type");
  }
}

void SetOperationLength(uint32_t length, block_op_t* txn) {
  switch (txn->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
      txn->rw.length = length;
      break;
    case BLOCK_OP_TRIM:
      txn->trim.length = length;
      break;
    default:
      ZX_ASSERT_MSG(false, "Unexpected operation type");
  }
}

}  // namespace.

VPartition::VPartition(VPartitionManager* vpm, size_t entry_index, size_t block_op_size)
    : PartitionDeviceType(vpm->zxdev()), mgr_(vpm), entry_index_(entry_index) {
  memcpy(&info_, &mgr_->Info(), sizeof(block_info_t));
  info_.block_count = 0;
}

VPartition::~VPartition() = default;

zx_status_t VPartition::Create(VPartitionManager* vpm, size_t entry_index,
                               std::unique_ptr<VPartition>* out) {
  ZX_ASSERT(entry_index != 0);

  auto vp = std::make_unique<VPartition>(vpm, entry_index, vpm->BlockOpSize());

  *out = std::move(vp);
  return ZX_OK;
}

bool VPartition::SliceGetLocked(uint64_t vslice, uint64_t* out_pslice) const {
  ZX_ASSERT(vslice < mgr_->VSliceMax());
  auto extent = --slice_map_.upper_bound(vslice);
  if (!extent.IsValid()) {
    return false;
  }
  ZX_ASSERT(extent->start() <= vslice);
  return extent->find(vslice, out_pslice);
}

zx_status_t VPartition::CheckSlices(uint64_t vslice_start, size_t* count, bool* allocated) {
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
    ZX_ASSERT(extent->start() <= vslice_start);
    if (extent->start() + extent->size() > vslice_start) {
      *count = extent->size() - (vslice_start - extent->start());
      *allocated = true;
    }
  }

  if (!(*allocated)) {
    auto extent = slice_map_.upper_bound(vslice_start);
    if (extent.IsValid()) {
      ZX_ASSERT(extent->start() > vslice_start);
      *count = extent->start() - vslice_start;
    } else {
      *count = mgr_->VSliceMax() - vslice_start;
    }
  }

  return ZX_OK;
}

void VPartition::SliceSetLocked(uint64_t vslice, uint64_t pslice) {
  ZX_ASSERT(vslice < mgr_->VSliceMax());
  auto extent = --slice_map_.upper_bound(vslice);
  ZX_ASSERT(!extent.IsValid() || !extent->contains(vslice));
  if (extent.IsValid() && (vslice == extent->end())) {
    // Easy case: append to existing slice
    extent->push_back(pslice);
  } else {
    // Longer case: there is no extent for this vslice, so we should make
    // one.
    std::unique_ptr<SliceExtent> new_extent(new SliceExtent(vslice));
    new_extent->push_back(pslice);
    slice_map_.insert(std::move(new_extent));
    extent = --slice_map_.upper_bound(vslice);
  }

  ZX_ASSERT(([this, vslice, pslice]() TA_NO_THREAD_SAFETY_ANALYSIS {
    uint64_t mapped_pslice;
    return SliceGetLocked(vslice, &mapped_pslice) && mapped_pslice == pslice;
  }()));
  AddBlocksLocked((mgr_->SliceSize() / info_.block_size));

  // Merge with the next contiguous extent (if any)
  auto next_extent = slice_map_.upper_bound(vslice);
  if (next_extent.IsValid() && (vslice + 1 == next_extent->start())) {
    extent->Merge(*next_extent);
    slice_map_.erase(*next_extent);
  }
}

void VPartition::SliceFreeLocked(uint64_t vslice) {
  ZX_ASSERT(vslice < mgr_->VSliceMax());
  ZX_ASSERT(SliceCanFree(vslice));
  auto extent = --slice_map_.upper_bound(vslice);
  if (vslice != extent->end() - 1) {
    // Removing from the middle of an extent; this splits the extent in
    // two.
    auto new_extent = extent->Split(vslice);
    slice_map_.insert(std::move(new_extent));
  }
  // Removing from end of extent
  extent->pop_back();
  if (extent->empty()) {
    slice_map_.erase(*extent);
  }

  AddBlocksLocked(-(mgr_->SliceSize() / info_.block_size));
}

void VPartition::ExtentDestroyLocked(uint64_t vslice) TA_REQ(lock_) {
  ZX_ASSERT(vslice < mgr_->VSliceMax());
  ZX_ASSERT(SliceCanFree(vslice));
  auto extent = --slice_map_.upper_bound(vslice);
  size_t length = extent->size();
  slice_map_.erase(*extent);
  AddBlocksLocked(-((length * mgr_->SliceSize()) / info_.block_size));
}

template <typename T>
static zx_status_t RequestBoundCheck(const T& request, uint64_t vslice_max) {
  if (request.offset == 0 || request.offset > vslice_max) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (request.length > vslice_max) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (request.offset + request.length < request.offset ||
      request.offset + request.length > vslice_max) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return ZX_OK;
}

// Device protocol (VPartition)

zx_status_t VPartition::DdkGetProtocol(uint32_t proto_id, void* out_protocol) {
  auto* proto = static_cast<ddk::AnyProtocol*>(out_protocol);
  proto->ctx = this;
  switch (proto_id) {
    case ZX_PROTOCOL_BLOCK_IMPL:
      proto->ops = &block_impl_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_BLOCK_PARTITION:
      proto->ops = &block_partition_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_BLOCK_VOLUME:
      proto->ops = &block_volume_protocol_ops_;
      return ZX_OK;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

class MultiTransactionState {
 public:
  MultiTransactionState(size_t total, block_op_t* txn, block_impl_queue_callback cb, void* cookie)
      : txns_completed_(0),
        txns_total_(total),
        status_(ZX_OK),
        original_(txn),
        completion_cb_(cb),
        cookie_(cookie) {}

  void Completion(zx_status_t status, block_op_t* txn) {
    bool last_txn = false;
    {
      fbl::AutoLock lock(&lock_);
      txns_completed_++;
      if (status_ == ZX_OK && status != ZX_OK) {
        status_ = status;
      }
      if (txns_completed_ == txns_total_) {
        last_txn = true;
        completion_cb_(cookie_, status_, original_);
      }
    }

    delete[] txn;
    if (last_txn) {
      delete this;
    }
  }

 private:
  fbl::Mutex lock_;
  size_t txns_completed_ TA_GUARDED(lock_);
  size_t txns_total_ TA_GUARDED(lock_);
  zx_status_t status_ TA_GUARDED(lock_);
  block_op_t* original_ TA_GUARDED(lock_);
  block_impl_queue_callback completion_cb_ TA_GUARDED(lock_);
  void* cookie_ TA_GUARDED(lock_);
};

static void MultiTransactionCompletion(void* cookie, zx_status_t status, block_op_t* txn) {
  MultiTransactionState* state = static_cast<MultiTransactionState*>(cookie);
  state->Completion(status, txn);
}

void VPartition::BlockImplQueue(block_op_t* txn, block_impl_queue_callback completion_cb,
                                void* cookie) {
  ZX_ASSERT(mgr_->BlockOpSize() > 0);
  uint32_t txn_length = 0;
  uint64_t offset_dev = 0;
  uint64_t offset_vmo = 0;
  switch (txn->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
      txn_length = txn->rw.length;
      offset_dev = txn->rw.offset_dev;
      offset_vmo = txn->rw.offset_vmo;
      break;
    case BLOCK_OP_TRIM:
      txn_length = txn->trim.length;
      offset_dev = txn->trim.offset_dev;
      break;

    // Pass-through operations
    case BLOCK_OP_FLUSH:
      mgr_->Queue(txn, completion_cb, cookie);
      return;
    default:
      zxlogf(ERROR, "[BlockQueue] Unsupported Command: %x", txn->command);
      completion_cb(cookie, ZX_ERR_NOT_SUPPORTED, txn);
      return;
  }

  const uint64_t device_capacity = DdkGetSize() / BlockSize();
  if (txn_length == 0) {
    completion_cb(cookie, ZX_ERR_INVALID_ARGS, txn);
    return;
  }

  if ((offset_dev >= device_capacity) || (device_capacity - offset_dev < txn_length)) {
    completion_cb(cookie, ZX_ERR_OUT_OF_RANGE, txn);
    return;
  }

  const FormatInfo& format_info = mgr_->format_info();
  const uint64_t slice_size = mgr_->SliceSize();
  const uint64_t blocks_per_slice = slice_size / BlockSize();
  // Start, end both inclusive
  uint64_t vslice_start = offset_dev / blocks_per_slice;
  uint64_t vslice_end = (offset_dev + txn_length - 1) / blocks_per_slice;

  fbl::AutoLock lock(&lock_);
  if (vslice_start == vslice_end) {
    // Common case: txn occurs within one slice
    uint64_t pslice;
    if (!SliceGetLocked(vslice_start, &pslice)) {
      completion_cb(cookie, ZX_ERR_OUT_OF_RANGE, txn);
      return;
    }
    offset_dev = format_info.GetSliceStart(pslice) / BlockSize() + (offset_dev % blocks_per_slice);
    SetOperationDeviceOffset(offset_dev, txn);
    mgr_->Queue(txn, completion_cb, cookie);
    return;
  }

  // Less common case: txn spans multiple slices

  // First, check that all slices are allocated.
  // If any are missing, then this txn will fail.
  bool contiguous = true;
  for (size_t vslice = vslice_start; vslice <= vslice_end; vslice++) {
    uint64_t pslice;
    if (!SliceGetLocked(vslice, &pslice)) {
      completion_cb(cookie, ZX_ERR_OUT_OF_RANGE, txn);
      return;
    }
    uint64_t prev_pslice;
    if (vslice != vslice_start && SliceGetLocked(vslice - 1, &prev_pslice) &&
        prev_pslice + 1 != pslice) {
      contiguous = false;
    }
  }

  // Ideal case: slices are contiguous
  if (contiguous) {
    uint64_t pslice;
    SliceGetLocked(vslice_start, &pslice);
    offset_dev = format_info.GetSliceStart(pslice) / BlockSize() + (offset_dev % blocks_per_slice);
    SetOperationDeviceOffset(offset_dev, txn);
    mgr_->Queue(txn, completion_cb, cookie);
    return;
  }

  // Harder case: Noncontiguous slices
  const uint64_t txn_count = vslice_end - vslice_start + 1;
  fbl::Vector<block_op_t*> txns;
  txns.reserve(txn_count);

  std::unique_ptr<MultiTransactionState> state(
      new MultiTransactionState(txn_count, txn, completion_cb, cookie));

  uint32_t length_remaining = txn_length;
  for (size_t i = 0; i < txn_count; i++) {
    uint64_t vslice = vslice_start + i;
    uint64_t pslice;
    SliceGetLocked(vslice, &pslice);

    uint64_t length;
    if (vslice == vslice_start) {
      length = fbl::round_up(offset_dev + 1, blocks_per_slice) - offset_dev;
    } else if (vslice == vslice_end) {
      length = length_remaining;
    } else {
      length = blocks_per_slice;
    }
    ZX_ASSERT(length <= blocks_per_slice);
    ZX_ASSERT(length <= length_remaining);

    txns.push_back(reinterpret_cast<block_op_t*>(new uint8_t[mgr_->BlockOpSize()]));

    memcpy(txns[i], txn, sizeof(*txn));
    uint64_t sub_txn_offset_dev = format_info.GetSliceStart(pslice) / BlockSize();
    if (vslice == vslice_start) {
      sub_txn_offset_dev += (offset_dev % blocks_per_slice);
    }
    SetOperationDeviceOffset(sub_txn_offset_dev, txns[i]);
    SetOperationVmoOffset(offset_vmo, txns[i]);
    SetOperationLength(static_cast<uint32_t>(length), txns[i]);
    offset_vmo += static_cast<uint32_t>(length);
    length_remaining -= static_cast<uint32_t>(length);
  }
  ZX_ASSERT(length_remaining == 0);

  for (size_t i = 0; i < txn_count; i++) {
    mgr_->Queue(txns[i], MultiTransactionCompletion, state.get());
  }
  // When mullti-transaction operation completes, the state gets deleted from
  // Completion() context. We should not be deleting it again.
  __UNUSED auto ptr = state.release();
}

void VPartition::BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out) {
  static_assert(std::is_same<decltype(info_out), decltype(&info_)>::value, "Info type mismatch");
  memcpy(info_out, &info_, sizeof(info_));
  *block_op_size_out = mgr_->BlockOpSize();
}

static_assert(fvm::kGuidSize == GUID_LENGTH, "Invalid GUID length");

zx_status_t VPartition::BlockPartitionGetGuid(guidtype_t guid_type, guid_t* out_guid) {
  fbl::AutoLock lock(&lock_);
  if (IsKilledLocked()) {
    return ZX_ERR_BAD_STATE;
  }

  switch (guid_type) {
    case GUIDTYPE_TYPE:
      memcpy(out_guid, mgr_->GetAllocatedVPartEntry(entry_index_)->type, fvm::kGuidSize);
      return ZX_OK;
    case GUIDTYPE_INSTANCE:
      memcpy(out_guid, mgr_->GetAllocatedVPartEntry(entry_index_)->guid, fvm::kGuidSize);
      return ZX_OK;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
}

static_assert(fvm::kMaxVPartitionNameLength < MAX_PARTITION_NAME_LENGTH, "Name Length mismatch");

zx_status_t VPartition::BlockPartitionGetName(char* out_name, size_t capacity) {
  if (capacity < fvm::kMaxVPartitionNameLength + 1) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  fbl::AutoLock lock(&lock_);
  if (IsKilledLocked()) {
    return ZX_ERR_BAD_STATE;
  }
  const std::string name = mgr_->GetAllocatedVPartEntry(entry_index_)->name();
  memcpy(out_name, name.c_str(), name.size() + 1);
  return ZX_OK;
}

zx_status_t VPartition::BlockVolumeExtend(const slice_extent_t* extent) {
  zx_status_t status = RequestBoundCheck(*extent, mgr_->VSliceMax());
  if (status != ZX_OK) {
    return status;
  }
  if (extent->length == 0) {
    return ZX_OK;
  }
  return mgr_->AllocateSlices(this, extent->offset, extent->length);
}

zx_status_t VPartition::BlockVolumeShrink(const slice_extent_t* extent) {
  zx_status_t status = RequestBoundCheck(*extent, mgr_->VSliceMax());
  if (status != ZX_OK) {
    return status;
  }
  if (extent->length == 0) {
    return ZX_OK;
  }
  return mgr_->FreeSlices(this, extent->offset, extent->length);
}

zx_status_t VPartition::BlockVolumeQuery(parent_volume_info_t* out_info) {
  // TODO(smklein): Ensure Banjo (parent_volume_info_t) and FIDL (volume_info_t)
  // are aligned.
  static_assert(sizeof(parent_volume_info_t) == sizeof(volume_info_t), "Info Mismatch");
  volume_info_t* info = reinterpret_cast<volume_info_t*>(out_info);
  mgr_->Query(info);
  return ZX_OK;
}

zx_status_t VPartition::BlockVolumeQuerySlices(const uint64_t* start_list, size_t start_count,
                                               slice_region_t* out_responses_list,
                                               size_t responses_count,
                                               size_t* out_responses_actual) {
  if ((start_count > MAX_SLICE_QUERY_REQUESTS) || (start_count > responses_count)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  for (size_t i = 0; i < start_count; i++) {
    zx_status_t status;
    if ((status = CheckSlices(start_list[i], &out_responses_list[i].count,
                              &out_responses_list[i].allocated)) != ZX_OK) {
      return status;
    }
  }
  *out_responses_actual = start_count;
  return ZX_OK;
}

zx_status_t VPartition::BlockVolumeDestroy() {
  return mgr_->FreeSlices(this, 0, mgr_->VSliceMax());
}

zx_off_t VPartition::DdkGetSize() {
  const zx_off_t sz = mgr_->VSliceMax() * mgr_->SliceSize();
  // Check for overflow; enforced when loading driver
  ZX_ASSERT(sz / mgr_->VSliceMax() == mgr_->SliceSize());
  return sz;
}

void VPartition::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

void VPartition::DdkRelease() { delete this; }

zx_device_t* VPartition::GetParent() const { return mgr_->parent(); }

}  // namespace fvm
