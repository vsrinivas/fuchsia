// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ramdisk.h"

#include <inttypes.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

#include <atomic>
#include <limits>
#include <memory>

#include <ddk/driver.h>
#include <fbl/auto_lock.h>

#include "zircon/errors.h"

namespace ramdisk {
namespace {

using Transaction = block::BorrowedOperation<>;

constexpr uint64_t kMaxTransferSize = 1LLU << 19;

static std::atomic<uint64_t> g_ramdisk_count = 0;

}  // namespace

Ramdisk::Ramdisk(zx_device_t* parent, uint64_t block_size, uint64_t block_count,
                 const uint8_t* type_guid, fzl::ResizeableVmoMapper mapping)
    : RamdiskDeviceType(parent),
      block_size_(block_size),
      block_count_(block_count),
      mapping_(std::move(mapping)) {
  if (type_guid) {
    memcpy(type_guid_, type_guid, ZBI_PARTITION_GUID_LEN);
  } else {
    memset(type_guid_, 0, ZBI_PARTITION_GUID_LEN);
  }
  snprintf(name_, sizeof(name_), "ramdisk-%" PRIu64, g_ramdisk_count.fetch_add(1));
}

zx_status_t Ramdisk::Create(zx_device_t* parent, zx::vmo vmo, uint64_t block_size,
                            uint64_t block_count, const uint8_t* type_guid,
                            std::unique_ptr<Ramdisk>* out) {
  fzl::ResizeableVmoMapper mapping;
  zx_status_t status = mapping.Map(std::move(vmo), block_size * block_count);
  if (status != ZX_OK) {
    return status;
  }

  auto ramdev = std::unique_ptr<Ramdisk>(
      new Ramdisk(parent, block_size, block_count, type_guid, std::move(mapping)));
  if (thrd_create(&ramdev->worker_, WorkerThunk, ramdev.get()) != thrd_success) {
    return ZX_ERR_NO_MEMORY;
  }

  *out = std::move(ramdev);
  return ZX_OK;
}

zx_status_t Ramdisk::DdkGetProtocol(uint32_t proto_id, void* out_protocol) {
  auto* proto = static_cast<ddk::AnyProtocol*>(out_protocol);
  proto->ctx = this;
  switch (proto_id) {
    case ZX_PROTOCOL_BLOCK_IMPL: {
      proto->ops = &block_impl_protocol_ops_;
      return ZX_OK;
    }
    case ZX_PROTOCOL_BLOCK_PARTITION: {
      proto->ops = &block_partition_protocol_ops_;
      return ZX_OK;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_off_t Ramdisk::DdkGetSize() { return block_size_ * block_count_; }

void Ramdisk::DdkUnbind(ddk::UnbindTxn txn) {
  {
    fbl::AutoLock lock(&lock_);
    dead_ = true;
  }
  sync_completion_signal(&signal_);
  txn.Reply();
}

zx_status_t Ramdisk::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_ramdisk_Ramdisk_dispatch(this, txn, msg, Ops());
}

void Ramdisk::DdkRelease() {
  // Wake up the worker thread, in case it is sleeping
  sync_completion_signal(&signal_);

  thrd_join(worker_, nullptr);
  delete this;
}

void Ramdisk::BlockImplQuery(block_info_t* info, size_t* bopsz) {
  memset(info, 0, sizeof(*info));
  info->block_size = static_cast<uint32_t>(block_size_);
  info->block_count = block_count_;
  // Arbitrarily set, but matches the SATA driver for testing
  info->max_transfer_size = kMaxTransferSize;
  fbl::AutoLock lock(&lock_);
  info->flags = flags_;
  *bopsz = Transaction::OperationSize(sizeof(block_op_t));
}

void Ramdisk::BlockImplQueue(block_op_t* bop, block_impl_queue_callback completion_cb,
                             void* cookie) {
  Transaction txn(bop, completion_cb, cookie, sizeof(block_op_t));
  bool dead;
  bool read = false;

  switch ((txn.operation()->command &= BLOCK_OP_MASK)) {
    case BLOCK_OP_READ:
      read = true;
      __FALLTHROUGH;
    case BLOCK_OP_WRITE:
      if ((txn.operation()->rw.offset_dev >= block_count_) ||
          ((block_count_ - txn.operation()->rw.offset_dev) < txn.operation()->rw.length)) {
        txn.Complete(ZX_ERR_OUT_OF_RANGE);
        return;
      }

      {
        fbl::AutoLock lock(&lock_);
        if (!(dead = dead_)) {
          if (!read) {
            block_counts_.received += txn.operation()->rw.length;
          }
          txn_list_.push(std::move(txn));
        }
      }

      if (dead) {
        txn.Complete(ZX_ERR_BAD_STATE);
      } else {
        sync_completion_signal(&signal_);
      }
      break;
    case BLOCK_OP_FLUSH:
      txn.Complete(ZX_OK);
      break;
    default:
      txn.Complete(ZX_ERR_NOT_SUPPORTED);
      break;
  }
}

zx_status_t Ramdisk::FidlSetFlags(uint32_t flags, fidl_txn_t* txn) {
  {
    fbl::AutoLock lock(&lock_);
    flags_ = flags;
  }
  return fuchsia_hardware_ramdisk_RamdiskSetFlags_reply(txn, ZX_OK);
}

zx_status_t Ramdisk::FidlWake(fidl_txn_t* txn) {
  {
    fbl::AutoLock lock(&lock_);
    asleep_ = false;
    memset(&block_counts_, 0, sizeof(block_counts_));
    pre_sleep_write_block_count_ = 0;
    sync_completion_signal(&signal_);
  }
  return fuchsia_hardware_ramdisk_RamdiskWake_reply(txn, ZX_OK);
}

zx_status_t Ramdisk::FidlSleepAfter(uint64_t block_count, fidl_txn_t* txn) {
  {
    fbl::AutoLock lock(&lock_);
    asleep_ = false;
    memset(&block_counts_, 0, sizeof(block_counts_));
    pre_sleep_write_block_count_ = block_count;

    if (block_count == 0) {
      asleep_ = true;
    }
  }
  return fuchsia_hardware_ramdisk_RamdiskSleepAfter_reply(txn, ZX_OK);
}

zx_status_t Ramdisk::FidlGetBlockCounts(fidl_txn_t* txn) {
  fuchsia_hardware_ramdisk_BlockWriteCounts block_counts;
  {
    fbl::AutoLock lock(&lock_);
    memcpy(&block_counts, &block_counts_, sizeof(block_counts_));
  }
  return fuchsia_hardware_ramdisk_RamdiskGetBlockCounts_reply(txn, ZX_OK, &block_counts);
}

zx_status_t Ramdisk::BlockPartitionGetGuid(guidtype_t guid_type, guid_t* out_guid) {
  if (guid_type != GUIDTYPE_TYPE) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  static_assert(ZBI_PARTITION_GUID_LEN == GUID_LENGTH, "GUID length mismatch");
  memcpy(out_guid, type_guid_, ZBI_PARTITION_GUID_LEN);
  return ZX_OK;
}

zx_status_t Ramdisk::BlockPartitionGetName(char* out_name, size_t capacity) {
  if (capacity < ZBI_PARTITION_NAME_LEN) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  static_assert(ZBI_PARTITION_NAME_LEN <= MAX_PARTITION_NAME_LENGTH, "Name length mismatch");
  strlcpy(out_name, name_, ZBI_PARTITION_NAME_LEN);
  return ZX_OK;
}

zx_status_t Ramdisk::FidlGrow(uint64_t required_size, fidl_txn_t* txn) {
  fbl::AutoLock lock(&lock_);
  if (required_size < block_size_ * block_count_) {
    return fuchsia_hardware_ramdisk_RamdiskGrow_reply(txn, ZX_ERR_INVALID_ARGS);
  }

  if (required_size % block_size_ != 0) {
    return fuchsia_hardware_ramdisk_RamdiskGrow_reply(txn, ZX_ERR_INVALID_ARGS);
  }
  zx_status_t status = mapping_.Grow(required_size);
  if (status != ZX_OK) {
    return fuchsia_hardware_ramdisk_RamdiskGrow_reply(txn, status);
  }

  block_count_ = required_size / block_size_;
  return fuchsia_hardware_ramdisk_RamdiskGrow_reply(txn, ZX_OK);
}

void Ramdisk::ProcessRequests() {
  zx_status_t status = ZX_OK;
  std::optional<Transaction> txn;
  bool dead, asleep, defer;
  uint64_t blocks = 0;
  block::BorrowedOperationQueue<> deferred_list;

  for (;;) {
    do {
      txn = std::nullopt;

      {
        fbl::AutoLock lock(&lock_);
        dead = dead_;
        asleep = asleep_;
        defer = (flags_ & fuchsia_hardware_ramdisk_RAMDISK_FLAG_RESUME_ON_WAKE) != 0;
        blocks = pre_sleep_write_block_count_;

        if (dead) {
          while ((txn = deferred_list.pop())) {
            txn->Complete(ZX_ERR_BAD_STATE);
          }
          while ((txn = txn_list_.pop())) {
            txn->Complete(ZX_ERR_BAD_STATE);
          }
          return;
        }

        if (!asleep) {
          // If we are awake, try grabbing pending transactions from the deferred list.
          txn = deferred_list.pop();
        }

        if (!txn) {
          // If no transactions were available in the deferred list (or we are asleep),
          // grab one from the regular txn_list.
          txn = txn_list_.pop();
        }
      }

      if (!txn) {
        sync_completion_wait(&signal_, ZX_TIME_INFINITE);
        sync_completion_reset(&signal_);
      }

    } while (!txn);

    uint64_t txn_blocks = txn->operation()->rw.length;
    if (txn->operation()->command == BLOCK_OP_READ || blocks == 0 || blocks > txn_blocks) {
      // If the ramdisk is not configured to sleep after x blocks, or the number of blocks in
      // this transaction does not exceed the pre_sleep_write_block_count, or we are
      // performing a read operation, use the current transaction length.
      blocks = txn_blocks;
    }

    size_t length = blocks * block_size_;
    size_t dev_offset = txn->operation()->rw.offset_dev * block_size_;
    size_t vmo_offset = txn->operation()->rw.offset_vmo * block_size_;
    void* addr =
        reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mapping_.start()) + dev_offset);
    auto command = txn->operation()->command;

    if (length > kMaxTransferSize) {
      status = ZX_ERR_OUT_OF_RANGE;
    } else if (command == BLOCK_OP_READ) {
      // A read operation should always succeed, even if the ramdisk is "asleep".
      status = zx_vmo_write(txn->operation()->rw.vmo, addr, vmo_offset, length);
    } else if (asleep) {
      if (defer) {
        // If we are asleep but resuming on wake, add txn to the deferred_list.
        // deferred_list is only accessed by the worker_thread, so a lock is not needed.
        deferred_list.push(std::move(*txn));
        continue;
      } else {
        status = ZX_ERR_UNAVAILABLE;
      }
    } else {  // BLOCK_OP_WRITE
      status = zx_vmo_read(txn->operation()->rw.vmo, addr, vmo_offset, length);

      if (status == ZX_OK && blocks < txn->operation()->rw.length && defer) {
        // If the first part of the transaction succeeded but the entire transaction is not
        // complete, we need to address the remainder.

        // If we are deferring after this block count, update the transaction to
        // reflect the blocks that have already been written, and add it to the
        // deferred queue.
        ZX_DEBUG_ASSERT_MSG(blocks <= std::numeric_limits<uint32_t>::max(), "Block count overflow");
        txn->operation()->rw.length -= static_cast<uint32_t>(blocks);
        txn->operation()->rw.offset_vmo += blocks;
        txn->operation()->rw.offset_dev += blocks;

        // Add the remaining blocks to the deferred list.
        deferred_list.push(std::move(*txn));
      }
    }

    if (command == BLOCK_OP_WRITE) {
      {
        // Update the ramdisk block counts. Since we aren't failing read transactions,
        // only include write transaction counts.
        fbl::AutoLock lock(&lock_);
        // Increment the count based on the result of the last transaction.
        if (status == ZX_OK) {
          block_counts_.successful += blocks;

          if (blocks != txn_blocks && !defer) {
            // If we are not deferring, then any excess blocks have failed.
            block_counts_.failed += txn_blocks - blocks;
            status = ZX_ERR_UNAVAILABLE;
          }
        } else {
          block_counts_.failed += txn_blocks;
        }

        // Put the ramdisk to sleep if we have reached the required # of blocks.
        if (pre_sleep_write_block_count_ > 0) {
          pre_sleep_write_block_count_ -= blocks;
          asleep_ = (pre_sleep_write_block_count_ == 0);
        }
      }

      if (defer && blocks != txn_blocks && status == ZX_OK) {
        // If we deferred partway through a transaction, hold off on returning the
        // result until the remainder of the transaction is completed.
        continue;
      }
    }

    txn->Complete(status);
  }
}

}  // namespace ramdisk
