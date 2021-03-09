// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block.h"

#include <inttypes.h>
#include <lib/zircon-internal/align.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <zircon/compiler.h>

#include <memory>
#include <utility>

#include <ddk/debug.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <pretty/hexdump.h>

#include "src/devices/bus/lib/virtio/trace.h"

#define LOCAL_TRACE 0

// 1MB max transfer (unless further restricted by ring size).
#define MAX_SCATTER 257

namespace virtio {

// Cache some page size calculations that are used frequently.
static const uint32_t kPageSize = zx_system_get_page_size();
static const uint32_t kPageMask = kPageSize - 1;
static const uint32_t kMaxMaxXfer = (MAX_SCATTER - 1) * kPageSize;

void BlockDevice::txn_complete(block_txn_t* txn, zx_status_t status) {
  if (txn->pmt != ZX_HANDLE_INVALID) {
    zx_pmt_unpin(txn->pmt);
    txn->pmt = ZX_HANDLE_INVALID;
  }
  txn->completion_cb(txn->cookie, status, &txn->op);
}

// DDK level ops

void BlockDevice::BlockImplQuery(block_info_t* info, size_t* bopsz) {
  memset(info, 0, sizeof(*info));
  info->block_size = GetBlockSize();
  info->block_count = DdkGetSize() / GetBlockSize();
  info->max_transfer_size = (uint32_t)(kPageSize * (ring_size - 2));

  // Limit max transfer to our worst case scatter list size.
  if (info->max_transfer_size > kMaxMaxXfer) {
    info->max_transfer_size = kMaxMaxXfer;
  }
  *bopsz = sizeof(block_txn_t);
}

void BlockDevice::BlockImplQueue(block_op_t* bop, block_impl_queue_callback completion_cb,
                                 void* cookie) {
  block_txn_t* txn = static_cast<block_txn_t*>((void*)bop);
  txn->pmt = ZX_HANDLE_INVALID;
  txn->completion_cb = completion_cb;
  txn->cookie = cookie;
  SignalWorker(txn);
}

zx_status_t BlockDevice::DdkGetProtocol(uint32_t proto_id, void* out) {
  auto* proto = static_cast<ddk::AnyProtocol*>(out);
  proto->ctx = this;
  if (proto_id == ZX_PROTOCOL_BLOCK_IMPL) {
    proto->ops = &block_impl_protocol_ops_;
    return ZX_OK;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

BlockDevice::BlockDevice(zx_device_t* bus_device, zx::bti bti, std::unique_ptr<Backend> backend)
    : virtio::Device(bus_device, std::move(bti), std::move(backend)), DeviceType(bus_device) {
  sync_completion_reset(&txn_signal_);
  sync_completion_reset(&worker_signal_);

  memset(&blk_req_buf_, 0, sizeof(blk_req_buf_));
}

zx_status_t BlockDevice::Init() {
  LTRACE_ENTRY;

  DeviceReset();
  CopyDeviceConfig(&config_, sizeof(config_));

  // TODO(cja): The blk_size provided in the device configuration is only
  // populated if a specific feature bit has been negotiated during
  // initialization, otherwise it is 0, at least in Virtio 0.9.5. Use 512
  // as a default as a stopgap for now until proper feature negotiation
  // is supported.
  if (config_.blk_size == 0)
    config_.blk_size = 512;

  LTRACEF("capacity %#" PRIx64 "\n", config_.capacity);
  LTRACEF("size_max %#x\n", config_.size_max);
  LTRACEF("seg_max  %#x\n", config_.seg_max);
  LTRACEF("blk_size %#x\n", config_.blk_size);

  DriverStatusAck();

  // TODO: Check features bits and ack/nak them

  // Allocate the main vring.
  auto err = vring_.Init(0, ring_size);
  if (err < 0) {
    zxlogf(ERROR, "failed to allocate vring");
    return err;
  }

  // Allocate a queue of block requests.
  size_t size = sizeof(virtio_blk_req_t) * blk_req_count + sizeof(uint8_t) * blk_req_count;

  zx_status_t status =
      io_buffer_init(&blk_req_buf_, bti_.get(), size, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    zxlogf(ERROR, "cannot alloc blk_req buffers %d", status);
    return status;
  }
  auto cleanup = fbl::MakeAutoCall([this]() { io_buffer_release(&blk_req_buf_); });
  blk_req_ = static_cast<virtio_blk_req_t*>(io_buffer_virt(&blk_req_buf_));

  LTRACEF("allocated blk request at %p, physical address %#" PRIxPTR "\n", blk_req_,
          io_buffer_phys(&blk_req_buf_));

  // Responses are 32 words at the end of the allocated block.
  blk_res_pa_ = io_buffer_phys(&blk_req_buf_) + sizeof(virtio_blk_req_t) * blk_req_count;
  blk_res_ = (uint8_t*)((uintptr_t)blk_req_ + sizeof(virtio_blk_req_t) * blk_req_count);

  LTRACEF("allocated blk responses at %p, physical address %#" PRIxPTR "\n", blk_res_, blk_res_pa_);

  StartIrqThread();
  DriverStatusOk();

  auto thread_entry = [](void* ctx) {
    auto bd = static_cast<BlockDevice*>(ctx);
    bd->WorkerThread();
    return ZX_OK;
  };
  int ret = thrd_create_with_name(&worker_thread_, thread_entry, this, "virtio-block-worker");
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  // Initialize and publish the zx_device.
  status = DdkAdd("virtio-block");
  device_ = zxdev();
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to run DdkAdd");
    device_ = nullptr;
    return status;
  }

  cleanup.cancel();
  return ZX_OK;
}

void BlockDevice::DdkRelease() {
  thrd_join(worker_thread_, nullptr);
  io_buffer_release(&blk_req_buf_);
  virtio::Device::Release();
}

void BlockDevice::DdkUnbind(ddk::UnbindTxn txn) {
  worker_shutdown_.store(true);
  sync_completion_signal(&worker_signal_);
  sync_completion_signal(&txn_signal_);
  virtio::Device::Unbind(std::move(txn));
}

void BlockDevice::IrqRingUpdate() {
  LTRACE_ENTRY;

  // Parse our descriptor chain and add back to the free queue.
  auto free_chain = [this](vring_used_elem* used_elem) {
    uint32_t i = (uint16_t)used_elem->id;
    struct vring_desc* desc = vring_.DescFromIndex((uint16_t)i);
    auto head_desc = desc;  // Save the first element.
    {
      fbl::AutoLock lock(&ring_lock_);
      for (;;) {
        int next;
        LTRACE_DO(virtio_dump_desc(desc));
        if (desc->flags & VRING_DESC_F_NEXT) {
          next = desc->next;
        } else {
          // End of chain.
          next = -1;
        }

        vring_.FreeDesc((uint16_t)i);

        if (next < 0)
          break;
        i = next;
        desc = vring_.DescFromIndex((uint16_t)i);
      }
    }

    bool need_complete = false;
    block_txn_t* txn = nullptr;
    {
      fbl::AutoLock lock(&txn_lock_);

      // Search our pending txn list to see if this completes it.
      list_for_every_entry (&pending_txn_list_, txn, block_txn_t, node) {
        if (txn->desc == head_desc) {
          LTRACEF("completes txn %p\n", txn);
          free_blk_req(txn->index);
          list_delete(&txn->node);

          // We will do this outside of the lock.
          need_complete = true;

          sync_completion_signal(&txn_signal_);
          break;
        }
      }
    }

    if (need_complete) {
      txn_complete(txn, ZX_OK);
    }
  };

  // Tell the ring to find free chains and hand it back to our lambda.
  vring_.IrqRingUpdate(free_chain);
}

void BlockDevice::IrqConfigChange() { LTRACE_ENTRY; }

zx_status_t BlockDevice::QueueTxn(block_txn_t* txn, uint32_t type, size_t bytes, zx_paddr_t* pages,
                                  size_t pagecount, uint16_t* idx) {
  size_t index;
  {
    fbl::AutoLock lock(&txn_lock_);
    index = alloc_blk_req();
    if (index >= blk_req_count) {
      LTRACEF("too many block requests queued (%zu)!\n", index);
      return ZX_ERR_NO_RESOURCES;
    }
  }

  auto req = &blk_req_[index];
  req->type = type;
  req->ioprio = 0;
  if (type == VIRTIO_BLK_T_FLUSH) {
    req->sector = 0;
  } else {
    req->sector = txn->op.rw.offset_dev;
  }
  LTRACEF("blk_req type %u ioprio %u sector %" PRIu64 "\n", req->type, req->ioprio, req->sector);

  // Save the request index so we can free it when we complete the transfer.
  txn->index = index;

  LTRACEF("page count %lu\n", pagecount);

  // Put together a transfer.
  uint16_t i;
  vring_desc* desc;
  {
    fbl::AutoLock lock(&ring_lock_);
    desc = vring_.AllocDescChain((uint16_t)(2u + pagecount), &i);
  }
  if (!desc) {
    LTRACEF("failed to allocate descriptor chain of length %zu\n", 2u + pagecount);
    fbl::AutoLock lock(&txn_lock_);
    free_blk_req(index);
    return ZX_ERR_NO_RESOURCES;
  }

  LTRACEF("after alloc chain desc %p, i %u\n", desc, i);

  // Point the txn at this head descriptor.
  txn->desc = desc;

  // Set up the descriptor pointing to the head.
  desc->addr = io_buffer_phys(&blk_req_buf_) + index * sizeof(virtio_blk_req_t);
  desc->len = sizeof(virtio_blk_req_t);
  desc->flags = VRING_DESC_F_NEXT;
  LTRACE_DO(virtio_dump_desc(desc));

  for (size_t n = 0; n < pagecount; n++) {
    desc = vring_.DescFromIndex(desc->next);
    desc->addr = pages[n];
    desc->len = (uint32_t)((bytes > kPageSize) ? kPageSize : bytes);
    if (n == 0) {
      // First entry may not be page aligned.
      size_t page0_offset = txn->op.rw.offset_vmo & kPageMask;

      // Adjust starting address.
      desc->addr += page0_offset;

      // Trim length if necessary.
      size_t max = kPageSize - page0_offset;
      if (desc->len > max) {
        desc->len = (uint32_t)max;
      }
    }
    desc->flags = VRING_DESC_F_NEXT;
    LTRACEF("pa %#lx, len %#x\n", desc->addr, desc->len);

    // Mark buffer as write-only if its a block read.
    if (type == VIRTIO_BLK_T_IN) {
      desc->flags |= VRING_DESC_F_WRITE;
    }

    bytes -= desc->len;
  }
  LTRACE_DO(virtio_dump_desc(desc));
  assert(bytes == 0);

  // Set up the descriptor pointing to the response.
  desc = vring_.DescFromIndex(desc->next);
  desc->addr = blk_res_pa_ + index;
  desc->len = 1;
  desc->flags = VRING_DESC_F_WRITE;
  LTRACE_DO(virtio_dump_desc(desc));

  *idx = i;
  return ZX_OK;
}

static zx_status_t pin_pages(zx_handle_t bti, block_txn_t* txn, size_t bytes, zx_paddr_t* pages,
                             size_t* num_pages) {
  uint64_t suboffset = txn->op.rw.offset_vmo & kPageMask;
  uint64_t aligned_offset = txn->op.rw.offset_vmo & ~kPageMask;
  size_t pin_size = ZX_ROUNDUP(suboffset + bytes, kPageSize);
  *num_pages = pin_size / kPageSize;
  if (*num_pages > MAX_SCATTER) {
    TRACEF("virtio: transaction too large\n");
    return ZX_ERR_INVALID_ARGS;
  }

  zx_handle_t vmo = txn->op.rw.vmo;
  zx_status_t status;
  if ((status = zx_bti_pin(bti, ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE, vmo, aligned_offset, pin_size,
                           pages, *num_pages, &txn->pmt)) != ZX_OK) {
    TRACEF("virtio: could not pin pages %d\n", status);
    return ZX_ERR_INTERNAL;
  }

  pages[0] += suboffset;
  return ZX_OK;
}

void BlockDevice::SignalWorker(block_txn_t* txn) {
  switch (txn->op.command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
      // Transaction must fit within device.
      if ((txn->op.rw.offset_dev >= config_.capacity) ||
          (config_.capacity - txn->op.rw.offset_dev < txn->op.rw.length)) {
        LTRACEF("request beyond the end of the device!\n");
        txn_complete(txn, ZX_ERR_OUT_OF_RANGE);
        return;
      }

      if (txn->op.rw.length == 0) {
        txn_complete(txn, ZX_OK);
        return;
      }
      LTRACEF("txn %p, command %#x\n", txn, txn->op.command);
      break;
    case BLOCK_OP_FLUSH:
      LTRACEF("txn %p, command FLUSH\n", txn);
      break;
    default:
      txn_complete(txn, ZX_ERR_NOT_SUPPORTED);
      return;
  }

  fbl::AutoLock lock(&lock_);
  if (worker_shutdown_.load()) {
    txn_complete(txn, ZX_ERR_IO_NOT_PRESENT);
    return;
  }
  list_add_tail(&worker_txn_list_, &txn->node);
  sync_completion_signal(&worker_signal_);
}

void BlockDevice::WorkerThread() {
  auto cleanup = fbl::MakeAutoCall([this]() { CleanupPendingTxns(); });
  block_txn_t* txn = nullptr;
  for (;;) {
    if (worker_shutdown_.load()) {
      return;
    }

    // Pull a txn off the list or wait to be signaled.
    {
      fbl::AutoLock lock(&lock_);
      txn = list_remove_head_type(&worker_txn_list_, block_txn_t, node);
    }
    if (!txn) {
      sync_completion_wait(&worker_signal_, ZX_TIME_INFINITE);
      sync_completion_reset(&worker_signal_);
      continue;
    }

    LTRACEF("WorkerThread handling txn %p\n", txn);

    uint32_t type;
    bool do_flush = false;
    size_t bytes;
    zx_paddr_t pages[MAX_SCATTER];
    size_t num_pages;
    zx_status_t status = ZX_OK;

    if ((txn->op.command & BLOCK_OP_MASK) == BLOCK_OP_FLUSH) {
      type = VIRTIO_BLK_T_FLUSH;
      bytes = 0;
      num_pages = 0;
      do_flush = true;
    } else {
      if ((txn->op.command & BLOCK_OP_MASK) == BLOCK_OP_WRITE) {
        type = VIRTIO_BLK_T_OUT;
      } else {
        type = VIRTIO_BLK_T_IN;
      }
      txn->op.rw.offset_vmo *= config_.blk_size;
      bytes = txn->op.rw.length * config_.blk_size;
      status = pin_pages(bti_.get(), txn, bytes, pages, &num_pages);
    }

    if (status != ZX_OK) {
      txn_complete(txn, status);
      continue;
    }

    // A flush operation should complete after any inflight transactions, so wait for all
    // pending txns to complete before submitting a flush txn. This is necessary because
    // a virtio block device may service requests in any order.
    if (do_flush) {
      FlushPendingTxns();
      if (worker_shutdown_.load()) {
        return;
      }
    }

    bool cannot_fail = false;
    for (;;) {
      uint16_t idx;
      status = QueueTxn(txn, type, bytes, pages, num_pages, &idx);
      if (status == ZX_OK) {
        fbl::AutoLock lock(&txn_lock_);
        list_add_tail(&pending_txn_list_, &txn->node);
        vring_.SubmitChain(idx);
        vring_.Kick();
        LTRACEF("WorkerThread submitted txn %p\n", txn);
        break;
      }

      if (cannot_fail) {
        TRACEF("virtio-block: failed to queue txn to hw: %d\n", status);
        {
          fbl::AutoLock lock(&txn_lock_);
          free_blk_req(txn->index);
        }
        txn_complete(txn, status);
        break;
      }

      {
        fbl::AutoLock lock(&txn_lock_);
        if (list_is_empty(&pending_txn_list_)) {
          // We hold the txn lock and the list is empty, if we fail this time around
          // there's no point in trying again.
          cannot_fail = true;
          continue;
        }

        // Reset the txn signal then wait for one of the pending txns to complete
        // outside the lock. This should mean that resources have been freed for the next
        // iteration. We cannot deadlock due to the reset because pending_txn_list_ is not
        // empty.
        sync_completion_reset(&txn_signal_);
      }

      sync_completion_wait(&txn_signal_, ZX_TIME_INFINITE);
      if (worker_shutdown_.load()) {
        return;
      }
    }

    // A flush operation should complete before any subsequent transactions. So, we wait for all
    // pending transactions (including the flush) to complete before continuing.
    if (do_flush) {
      FlushPendingTxns();
    }
  }
}

void BlockDevice::FlushPendingTxns() {
  for (;;) {
    {
      fbl::AutoLock lock(&txn_lock_);
      if (list_is_empty(&pending_txn_list_)) {
        return;
      }
      sync_completion_reset(&txn_signal_);
    }
    sync_completion_wait(&txn_signal_, ZX_TIME_INFINITE);
    if (worker_shutdown_.load()) {
      return;
    }
  }
}

void BlockDevice::CleanupPendingTxns() {
  // Virtio specification 3.3.1 Driver Requirements: Device Cleanup
  // A driver MUST ensure a virtqueue isn’t live (by device reset) before removing exposed
  // buffers.
  DeviceReset();
  block_txn_t* txn = nullptr;
  block_txn_t* temp_entry = nullptr;
  {
    fbl::AutoLock lock(&lock_);
    list_for_every_entry_safe (&worker_txn_list_, txn, temp_entry, block_txn_t, node) {
      list_delete(&txn->node);
      txn_complete(txn, ZX_ERR_IO_NOT_PRESENT);
    }
  }
  fbl::AutoLock lock(&txn_lock_);
  list_for_every_entry_safe (&pending_txn_list_, txn, temp_entry, block_txn_t, node) {
    free_blk_req(txn->index);
    list_delete(&txn->node);
    txn_complete(txn, ZX_ERR_IO_NOT_PRESENT);
  }
}

}  // namespace virtio
