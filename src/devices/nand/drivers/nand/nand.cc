// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nand.h"

#include <lib/zx/time.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/threads.h>

#include <algorithm>
#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/io-buffer.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>

// TODO: Investigate elimination of unmap.
// This code does vx_vmar_map/unmap and copies data in/out of the
// mapped virtual address. Unmapping is expensive, but required (a closing
// of the vmo does not unmap, so not unmapping will quickly lead to memory
// exhaustion. Check to see if we can do something different - is vmo_read/write
// cheaper than mapping and unmapping (which will cause TLB flushes) ?

namespace nand {
namespace {

constexpr size_t kNandReadRetries = 3;

}  // namespace

zx_status_t NandDevice::ReadPage(void* data, void* oob, uint32_t nand_page,
                                 uint32_t* corrected_bits, size_t retries) {
  zx_status_t status = ZX_ERR_INTERNAL;

  size_t retry = 0;
  for (; status != ZX_OK && retry < retries; retry++) {
    status = raw_nand_.ReadPageHwecc(nand_page, data, nand_info_.page_size, nullptr, oob,
                                     nand_info_.oob_size, nullptr, corrected_bits);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Retrying Read@%u", __func__, nand_page);
    }
  }
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Read error %d, exhausted all retries", __func__, status);
  }
  if (retry > 1) {
    zxlogf(INFO, "%s: Successfuly read@%u on retry %zd", __func__, nand_page, retry - 1);
  }
  return status;
}

zx_status_t NandDevice::EraseOp(nand_operation_t* nand_op) {
  uint32_t nand_page;

  for (uint32_t i = 0; i < nand_op->erase.num_blocks; i++) {
    nand_page = (nand_op->erase.first_block + i) * nand_info_.pages_per_block;
    zx_status_t status = raw_nand_.EraseBlock(nand_page);
    if (status != ZX_OK) {
      zxlogf(ERROR, "nand: Erase of block %u failed", nand_op->erase.first_block + i);
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t NandDevice::MapVmos(const nand_operation_t& nand_op, fzl::VmoMapper* data,
                                uint8_t** vaddr_data, fzl::VmoMapper* oob, uint8_t** vaddr_oob) {
  zx_status_t status;
  if (nand_op.rw.data_vmo != ZX_HANDLE_INVALID) {
    const auto vmo = zx::unowned_vmo(nand_op.rw.data_vmo);
    const size_t offset_bytes = nand_op.rw.offset_data_vmo * nand_info_.page_size;
    const size_t aligned_offset_bytes =
        fbl::round_down(offset_bytes, static_cast<size_t>(PAGE_SIZE));
    const size_t page_offset_bytes = offset_bytes - aligned_offset_bytes;
    status = data->Map(*vmo, aligned_offset_bytes,
                       nand_op.rw.length * nand_info_.page_size + page_offset_bytes,
                       ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
    if (status != ZX_OK) {
      zxlogf(ERROR, "nand: Cannot map data vmo: %s", zx_status_get_string(status));
      return status;
    }
    *vaddr_data = reinterpret_cast<uint8_t*>(data->start()) + page_offset_bytes;
  }

  // Map oob.
  if (nand_op.rw.oob_vmo != ZX_HANDLE_INVALID) {
    const auto vmo = zx::unowned_vmo(nand_op.rw.oob_vmo);
    const size_t offset_bytes = nand_op.rw.offset_oob_vmo * nand_info_.page_size;
    const size_t aligned_offset_bytes =
        fbl::round_down(offset_bytes, static_cast<size_t>(PAGE_SIZE));
    const size_t page_offset_bytes = offset_bytes - aligned_offset_bytes;
    status = oob->Map(*vmo, aligned_offset_bytes,
                      nand_op.rw.length * nand_info_.oob_size + page_offset_bytes,
                      ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
    if (status != ZX_OK) {
      zxlogf(ERROR, "nand: Cannot map oob vmo: %s", zx_status_get_string(status));
      return status;
    }
    *vaddr_oob = reinterpret_cast<uint8_t*>(oob->start()) + page_offset_bytes;
  }
  return ZX_OK;
}

zx_status_t NandDevice::ReadOp(nand_operation_t* nand_op) {
  fzl::VmoMapper data;
  fzl::VmoMapper oob;
  uint8_t* vaddr_data = nullptr;
  uint8_t* vaddr_oob = nullptr;

  zx_status_t status = MapVmos(*nand_op, &data, &vaddr_data, &oob, &vaddr_oob);
  if (status != ZX_OK) {
    return status;
  }

  uint32_t max_corrected_bits = 0;
  for (uint32_t i = 0; i < nand_op->rw.length; i++) {
    uint32_t ecc_correct = 0;
    status = ReadPage(vaddr_data, vaddr_oob, nand_op->rw.offset_nand + i, &ecc_correct,
                      kNandReadRetries);
    if (status != ZX_OK) {
      zxlogf(ERROR, "nand: Read data error %d at page offset %u", status,
             nand_op->rw.offset_nand + i);
      break;
    } else {
      max_corrected_bits = std::max(max_corrected_bits, ecc_correct);
    }

    if (vaddr_data) {
      vaddr_data += nand_info_.page_size;
    }
    if (vaddr_oob) {
      vaddr_oob += nand_info_.oob_size;
    }
  }
  nand_op->rw.corrected_bit_flips = max_corrected_bits;

  return status;
}

zx_status_t NandDevice::WriteOp(nand_operation_t* nand_op) {
  fzl::VmoMapper data;
  fzl::VmoMapper oob;
  uint8_t* vaddr_data = nullptr;
  uint8_t* vaddr_oob = nullptr;

  zx_status_t status = MapVmos(*nand_op, &data, &vaddr_data, &oob, &vaddr_oob);
  if (status != ZX_OK) {
    return status;
  }

  for (uint32_t i = 0; i < nand_op->rw.length; i++) {
    status = raw_nand_.WritePageHwecc(vaddr_data, nand_info_.page_size, vaddr_oob,
                                      nand_info_.oob_size, nand_op->rw.offset_nand + i);
    if (status != ZX_OK) {
      zxlogf(ERROR, "nand: Write data error %d at page offset %u", status,
             nand_op->rw.offset_nand + i);
      break;
    }

    if (vaddr_data) {
      vaddr_data += nand_info_.page_size;
    }
    if (vaddr_oob) {
      vaddr_oob += nand_info_.oob_size;
    }
  }

  return status;
}

void NandDevice::DoIo(Transaction txn) {
  zx_status_t status = ZX_OK;

  switch (txn.operation()->command) {
    case NAND_OP_READ:
      status = ReadOp(txn.operation());
      break;
    case NAND_OP_WRITE:
      status = WriteOp(txn.operation());
      break;
    case NAND_OP_ERASE:
      status = EraseOp(txn.operation());
      break;
    default:
      ZX_DEBUG_ASSERT(false);  // Unexpected.
  }
  txn.Complete(status);
}

// Initialization is complete by the time the thread starts.
zx_status_t NandDevice::WorkerThread() {
  for (;;) {
    fbl::AutoLock al(&lock_);
    while (txn_queue_.is_empty() && !shutdown_) {
      worker_event_.Wait(&lock_);
    }
    if (shutdown_) {
      break;
    }
    nand::BorrowedOperationQueue<> queue(std::move(txn_queue_));
    al.release();
    auto txn = queue.pop();
    while (txn != std::nullopt) {
      DoIo(*std::move(txn));
      txn = queue.pop();
    }
  }

  zxlogf(TRACE, "nand: worker thread terminated");
  return ZX_OK;
}

void NandDevice::NandQuery(fuchsia_hardware_nand_Info* info_out, size_t* nand_op_size_out) {
  memcpy(info_out, &nand_info_, sizeof(*info_out));
  *nand_op_size_out = Transaction::OperationSize(sizeof(nand_operation_t));
}

void NandDevice::NandQueue(nand_operation_t* op, nand_queue_callback completion_cb, void* cookie) {
  if (completion_cb == nullptr) {
    zxlogf(TRACE, "nand: nand op %p completion_cb unset!", op);
    zxlogf(TRACE, "nand: cannot queue command!");
    return;
  }

  Transaction txn(op, completion_cb, cookie, sizeof(nand_operation_t));

  switch (op->command) {
    case NAND_OP_READ:
    case NAND_OP_WRITE: {
      if (op->rw.offset_nand >= num_nand_pages_ || !op->rw.length ||
          (num_nand_pages_ - op->rw.offset_nand) < op->rw.length) {
        txn.Complete(ZX_ERR_OUT_OF_RANGE);
        return;
      }
      if (op->rw.data_vmo == ZX_HANDLE_INVALID && op->rw.oob_vmo == ZX_HANDLE_INVALID) {
        txn.Complete(ZX_ERR_BAD_HANDLE);
        return;
      }
      break;
    }
    case NAND_OP_ERASE:
      if (!op->erase.num_blocks || op->erase.first_block >= nand_info_.num_blocks ||
          (op->erase.num_blocks > (nand_info_.num_blocks - op->erase.first_block))) {
        txn.Complete(ZX_ERR_OUT_OF_RANGE);
        return;
      }
      break;

    default:
      txn.Complete(ZX_ERR_NOT_SUPPORTED);
      return;
  }

  // TODO: UPDATE STATS HERE.
  fbl::AutoLock al(&lock_);
  txn_queue_.push(std::move(txn));
  worker_event_.Signal();
}

zx_status_t NandDevice::NandGetFactoryBadBlockList(uint32_t* bad_blocks, size_t bad_block_len,
                                                   size_t* num_bad_blocks) {
  *num_bad_blocks = 0;
  return ZX_ERR_NOT_SUPPORTED;
}

void NandDevice::DdkRelease() { delete this; }

NandDevice::~NandDevice() {
  // Signal the worker thread and wait for it to terminate.
  {
    fbl::AutoLock al(&lock_);
    shutdown_ = true;
    worker_event_.Signal();
  }
  thrd_join(worker_thread_, nullptr);

  // Error out all pending requests.
  fbl::AutoLock al(&lock_);
  txn_queue_.Release();
}

// static
zx_status_t NandDevice::Create(void* ctx, zx_device_t* parent) {
  zxlogf(ERROR, "NandDevice::Create: Starting...!");

  fbl::AllocChecker ac;
  std::unique_ptr<NandDevice> dev(new (&ac) NandDevice(parent));
  if (!ac.check()) {
    zxlogf(ERROR, "nand: no memory to allocate nand device!");
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status;
  if ((status = dev->Init()) != ZX_OK) {
    return status;
  }

  if ((status = dev->Bind()) != ZX_OK) {
    dev.release()->DdkRelease();
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = dev.release();

  return ZX_OK;
}

zx_status_t NandDevice::Init() {
  if (!raw_nand_.is_valid()) {
    zxlogf(ERROR, "nand: failed to get raw_nand protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status = raw_nand_.GetNandInfo(&nand_info_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "nand: get_nand_info returned error %d", status);
    return status;
  }

  num_nand_pages_ = nand_info_.num_blocks * nand_info_.pages_per_block;

  int rc = thrd_create_with_name(
      &worker_thread_, [](void* arg) { return static_cast<NandDevice*>(arg)->WorkerThread(); },
      this, "nand-worker");

  if (rc != thrd_success) {
    return thrd_status_to_zx_status(rc);
  }

  return ZX_OK;
}

zx_status_t NandDevice::Bind() {
  zx_device_prop_t props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_NAND},
      {BIND_NAND_CLASS, 0, fuchsia_hardware_nand_Class_PARTMAP},
  };

  return DdkAdd("nand", 0, props, fbl::count_of(props));
}

#ifndef TEST
static constexpr zx_driver_ops_t nand_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = NandDevice::Create;
  return ops;
}();
#endif

}  // namespace nand

#ifndef TEST
// The formatter does not play nice with these macros.
// clang-format off
ZIRCON_DRIVER_BEGIN(nand, nand::nand_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_RAW_NAND),
ZIRCON_DRIVER_END(nand)
// clang-format on
#endif
