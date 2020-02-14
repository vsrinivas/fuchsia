// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdmmc-block-device.h"

#include <inttypes.h>
#include <lib/fzl/vmo-mapper.h>
#include <zircon/hw/gpt.h>
#include <zircon/process.h>
#include <zircon/threads.h>

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <hw/sdmmc.h>

namespace {

constexpr size_t kTranMaxAttempts = 10;

constexpr uint32_t kBlockOp(uint32_t op) { return op & BLOCK_OP_MASK; }

constexpr uint32_t kBootSizeMultiplier = 128'000;

}  // namespace

namespace sdmmc {

zx_status_t PartitionDevice::AddDevice() {
  switch (partition_) {
    case USER_DATA_PARTITION:
      return DdkAdd("user");
    case BOOT_PARTITION_1:
      return DdkAdd("boot1");
    case BOOT_PARTITION_2:
      return DdkAdd("boot2");
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_off_t PartitionDevice::DdkGetSize() { return block_info_.block_count * block_info_.block_size; }

zx_status_t PartitionDevice::DdkGetProtocol(uint32_t proto_id, void* out) {
  auto* proto = reinterpret_cast<ddk::AnyProtocol*>(out);
  switch (proto_id) {
    case ZX_PROTOCOL_BLOCK_IMPL: {
      proto->ops = &block_impl_protocol_ops_;
      proto->ctx = this;
      return ZX_OK;
    }
    case ZX_PROTOCOL_BLOCK_PARTITION: {
      if (partition_ == USER_DATA_PARTITION) {
        return ZX_ERR_NOT_SUPPORTED;
      }

      proto->ops = &block_partition_protocol_ops_;
      proto->ctx = this;
      return ZX_OK;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

void PartitionDevice::BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out) {
  memcpy(info_out, &block_info_, sizeof(*info_out));
  *block_op_size_out = BlockOperation::OperationSize(sizeof(block_op_t));
}

void PartitionDevice::BlockImplQueue(block_op_t* btxn, block_impl_queue_callback completion_cb,
                                     void* cookie) {
  BlockOperation txn(btxn, completion_cb, cookie, sizeof(block_op_t));
  txn.private_storage()->partition = partition_;
  txn.private_storage()->block_count = block_info_.block_count;
  sdmmc_parent_->Queue(std::move(txn));
}

zx_status_t PartitionDevice::BlockPartitionGetGuid(guidtype_t guid_type, guid_t* out_guid) {
  ZX_DEBUG_ASSERT(partition_ != USER_DATA_PARTITION);

  constexpr uint8_t kGuidEmmcBoot1Value[] = GUID_EMMC_BOOT1_VALUE;
  constexpr uint8_t kGuidEmmcBoot2Value[] = GUID_EMMC_BOOT2_VALUE;

  switch (guid_type) {
    case GUIDTYPE_TYPE:
      if (partition_ == BOOT_PARTITION_1) {
        memcpy(out_guid->value, kGuidEmmcBoot1Value, GUID_LENGTH);
      } else {
        memcpy(out_guid->value, kGuidEmmcBoot2Value, GUID_LENGTH);
      }
      return ZX_OK;
    case GUIDTYPE_INSTANCE:
      return ZX_ERR_NOT_SUPPORTED;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
}

zx_status_t PartitionDevice::BlockPartitionGetName(char* out_name, size_t capacity) {
  ZX_DEBUG_ASSERT(partition_ != USER_DATA_PARTITION);
  if (capacity <= strlen(name())) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  strlcpy(out_name, name(), capacity);

  return ZX_OK;
}

zx_status_t SdmmcBlockDevice::Create(zx_device_t* parent, const SdmmcDevice& sdmmc,
                                     std::unique_ptr<SdmmcBlockDevice>* out_dev) {
  fbl::AllocChecker ac;
  out_dev->reset(new (&ac) SdmmcBlockDevice(parent, sdmmc));
  if (!ac.check()) {
    zxlogf(ERROR, "sdmmc: failed to allocate device memory\n");
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

zx_status_t SdmmcBlockDevice::AddDevice() {
  // Device must be in TRAN state at this point
  zx_status_t st = WaitForTran();
  if (st != ZX_OK) {
    zxlogf(ERROR, "sdmmc: waiting for TRAN state failed, retcode = %d\n", st);
    return ZX_ERR_TIMED_OUT;
  }

  fbl::AutoLock lock(&lock_);

  int rc = thrd_create_with_name(
      &worker_thread_,
      [](void* ctx) -> int { return reinterpret_cast<SdmmcBlockDevice*>(ctx)->WorkerThread(); },
      this, "sdmmc-block-worker");
  if (rc != thrd_success) {
    zxlogf(ERROR, "sdmmc: Failed to start worker thread, retcode = %d\n", st);
    return thrd_status_to_zx_status(rc);
  }

  st = DdkAdd(is_sd_ ? "sdmmc-sd" : "sdmmc-mmc", DEVICE_ADD_NON_BINDABLE);
  if (st != ZX_OK) {
    zxlogf(ERROR, "sdmmc: Failed to add block device, retcode = %d\n", st);
    return st;
  }

  fbl::AutoCall remove_device_on_error([&]() { DdkAsyncRemove(); });

  fbl::AllocChecker ac;
  std::unique_ptr<PartitionDevice> user_partition(
      new (&ac) PartitionDevice(zxdev(), this, block_info_, USER_DATA_PARTITION));
  if (!ac.check()) {
    zxlogf(ERROR, "sdmmc: failed to allocate device memory\n");
    return ZX_ERR_NO_MEMORY;
  }

  if ((st = user_partition->AddDevice()) != ZX_OK) {
    zxlogf(ERROR, "sdmmc: failed to add user partition device: %d\n", st);
    return st;
  }

  __UNUSED auto* dummy = user_partition.release();

  const uint32_t boot_size = raw_ext_csd_[MMC_EXT_CSD_BOOT_SIZE_MULT] * kBootSizeMultiplier;
  const bool boot_enabled =
      raw_ext_csd_[MMC_EXT_CSD_PARTITION_CONFIG] & MMC_EXT_CSD_BOOT_PARTITION_ENABLE_MASK;
  if (is_sd_ || boot_size == 0 || !boot_enabled) {
    remove_device_on_error.cancel();
    return ZX_OK;
  }

  const uint64_t boot_partition_block_count = boot_size / block_info_.block_size;
  const block_info_t boot_info = {
      .block_count = boot_partition_block_count,
      .block_size = block_info_.block_size,
      .max_transfer_size = block_info_.max_transfer_size,
      .flags = block_info_.flags,
      .reserved = 0,
  };

  std::unique_ptr<PartitionDevice> boot_partition_1(
      new (&ac) PartitionDevice(zxdev(), this, boot_info, BOOT_PARTITION_1));
  if (!ac.check()) {
    zxlogf(ERROR, "sdmmc: failed to allocate device memory\n");
    return ZX_ERR_NO_MEMORY;
  }

  std::unique_ptr<PartitionDevice> boot_partition_2(
      new (&ac) PartitionDevice(zxdev(), this, boot_info, BOOT_PARTITION_2));
  if (!ac.check()) {
    zxlogf(ERROR, "sdmmc: failed to allocate device memory\n");
    return ZX_ERR_NO_MEMORY;
  }

  if ((st = boot_partition_1->AddDevice()) != ZX_OK) {
    zxlogf(ERROR, "sdmmc: failed to add boot partition device: %d\n", st);
    return st;
  }

  dummy = boot_partition_1.release();

  if ((st = boot_partition_2->AddDevice()) != ZX_OK) {
    zxlogf(ERROR, "sdmmc: failed to add boot partition device: %d\n", st);
    return st;
  }

  dummy = boot_partition_2.release();

  remove_device_on_error.cancel();
  return ZX_OK;
}

void SdmmcBlockDevice::DdkUnbindNew(ddk::UnbindTxn txn) {
  StopWorkerThread();
  txn.Reply();
}

void SdmmcBlockDevice::StopWorkerThread() {
  dead_ = true;

  if (worker_thread_) {
    {
      fbl::AutoLock lock(&lock_);
      worker_event_.Broadcast();
    }

    thrd_join(worker_thread_, nullptr);
    worker_thread_ = 0;

    // error out all pending requests
    trace_async_id_t async_id = async_id_;
    lock_.Acquire();
    for (std::optional<BlockOperation> txn = txn_list_.pop(); txn; txn = txn_list_.pop()) {
      lock_.Release();
      BlockComplete(&(*txn), ZX_ERR_BAD_STATE, async_id);
      lock_.Acquire();
    }
    lock_.Release();
  }
}

void SdmmcBlockDevice::BlockComplete(BlockOperation* txn, zx_status_t status,
                                     trace_async_id_t async_id) {
  const block_op_t* bop = txn->operation();
  if (txn->node()->complete_cb()) {
    // If tracing is not enabled this is a no-op.
    TRACE_ASYNC_END("sdmmc", "sdmmc_do_txn", async_id_, "command", TA_INT32(bop->rw.command),
                    "extra", TA_INT32(bop->rw.extra), "length", TA_INT32(bop->rw.length),
                    "offset_vmo", TA_INT64(bop->rw.offset_vmo), "offset_dev",
                    TA_INT64(bop->rw.offset_dev), "txn_status", TA_INT32(status));
    txn->Complete(status);
  } else {
    zxlogf(TRACE, "sdmmc: block op %p completion_cb unset!\n", bop);
  }
}

void SdmmcBlockDevice::DoTxn(BlockOperation* txn) {
  // The TRACE_*() event macros are empty if driver tracing isn't enabled.
  // But that doesn't work for our call to trace_state().
  if (TRACE_ENABLED()) {
    async_id_ = TRACE_NONCE();
    TRACE_ASYNC_BEGIN("sdmmc", "sdmmc_do_txn", async_id_, "command",
                      TA_INT32(txn->operation()->rw.command), "extra",
                      TA_INT32(txn->operation()->rw.extra), "length",
                      TA_INT32(txn->operation()->rw.length), "offset_vmo",
                      TA_INT64(txn->operation()->rw.offset_vmo), "offset_dev",
                      TA_INT64(txn->operation()->rw.offset_dev));
  }

  uint32_t cmd_idx = 0;
  uint32_t cmd_flags = 0;

  // Figure out which SD command we need to issue.
  switch (kBlockOp(txn->operation()->command)) {
    case BLOCK_OP_READ:
      if (txn->operation()->rw.length > 1) {
        cmd_idx = SDMMC_READ_MULTIPLE_BLOCK;
        cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS;
        if (sdmmc_.host_info().caps & SDMMC_HOST_CAP_AUTO_CMD12) {
          cmd_flags |= SDMMC_CMD_AUTO12;
        }
      } else {
        cmd_idx = SDMMC_READ_BLOCK;
        cmd_flags = SDMMC_READ_BLOCK_FLAGS;
      }
      break;
    case BLOCK_OP_WRITE:
      if (txn->operation()->rw.length > 1) {
        cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK;
        cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS;
        if (sdmmc_.host_info().caps & SDMMC_HOST_CAP_AUTO_CMD12) {
          cmd_flags |= SDMMC_CMD_AUTO12;
        }
      } else {
        cmd_idx = SDMMC_WRITE_BLOCK;
        cmd_flags = SDMMC_WRITE_BLOCK_FLAGS;
      }
      break;
    case BLOCK_OP_FLUSH:
      BlockComplete(txn, ZX_OK, async_id_);
      return;
    default:
      // should not get here
      zxlogf(ERROR, "sdmmc: do_txn invalid block op %d\n", kBlockOp(txn->operation()->command));
      ZX_DEBUG_ASSERT(true);
      BlockComplete(txn, ZX_ERR_INVALID_ARGS, async_id_);
      return;
  }

  zx_status_t st = ZX_OK;
  if (!is_sd_ && txn->private_storage()->partition != current_partition_) {
    const uint8_t partition_config_value =
        (raw_ext_csd_[MMC_EXT_CSD_PARTITION_CONFIG] & MMC_EXT_CSD_PARTITION_ACCESS_MASK) |
        txn->private_storage()->partition;
    if ((st = MmcDoSwitch(MMC_EXT_CSD_PARTITION_CONFIG, partition_config_value)) != ZX_OK) {
      zxlogf(ERROR, "sdmmc: failed to switch to partition %u\n", txn->private_storage()->partition);
      BlockComplete(txn, st, async_id_);
      return;
    }
  }

  current_partition_ = txn->private_storage()->partition;

  zxlogf(TRACE,
         "sdmmc: do_txn blockop 0x%x offset_vmo 0x%" PRIx64
         " length 0x%x blocksize 0x%x"
         " max_transfer_size 0x%x\n",
         txn->operation()->command, txn->operation()->rw.offset_vmo, txn->operation()->rw.length,
         block_info_.block_size, block_info_.max_transfer_size);

  sdmmc_req_t* req = &req_;
  memset(req, 0, sizeof(*req));
  req->cmd_idx = cmd_idx;
  req->cmd_flags = cmd_flags;
  req->arg = static_cast<uint32_t>(txn->operation()->rw.offset_dev);
  req->blockcount = static_cast<uint16_t>(txn->operation()->rw.length);
  req->blocksize = static_cast<uint16_t>(block_info_.block_size);

  // convert offset_vmo and length to bytes
  uint64_t offset_vmo = txn->operation()->rw.offset_vmo * block_info_.block_size;
  uint64_t length = txn->operation()->rw.length * block_info_.block_size;

  fzl::VmoMapper mapper;

  if (sdmmc_.UseDma()) {
    req->use_dma = true;
    req->virt_buffer = nullptr;
    req->pmt = ZX_HANDLE_INVALID;
    req->dma_vmo = txn->operation()->rw.vmo;
    req->buf_offset = offset_vmo;
  } else {
    req->use_dma = false;
    st = mapper.Map(*zx::unowned_vmo(txn->operation()->rw.vmo), offset_vmo, length,
                    ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
    if (st != ZX_OK) {
      zxlogf(TRACE, "sdmmc: do_txn vmo map error %d\n", st);
      BlockComplete(txn, st, async_id_);
      return;
    }
    req->virt_buffer = mapper.start();
    req->virt_size = length;
  }

  st = sdmmc_.host().Request(req);

  if (st != ZX_OK || ((req->blockcount > 1) && !(req->cmd_flags & SDMMC_CMD_AUTO12))) {
    zx_status_t stop_st = sdmmc_.SdmmcStopTransmission();
    if (stop_st != ZX_OK) {
      zxlogf(TRACE, "sdmmc: do_txn stop transmission error %d\n", stop_st);
    }
  }

  if (st != ZX_OK) {
    zxlogf(TRACE, "sdmmc: do_txn error %d\n", st);
  }

  BlockComplete(txn, st, async_id_);
  zxlogf(TRACE, "sdmmc: do_txn complete\n");
}

void SdmmcBlockDevice::Queue(BlockOperation txn) {
  trace_async_id_t async_id = async_id_;
  block_op_t* btxn = txn.operation();

  switch (kBlockOp(btxn->command)) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE: {
      const uint64_t max = txn.private_storage()->block_count;
      if ((btxn->rw.offset_dev >= max) || ((max - btxn->rw.offset_dev) < btxn->rw.length)) {
        BlockComplete(&txn, ZX_ERR_OUT_OF_RANGE, async_id);
        return;
      }
      if (btxn->rw.length == 0) {
        BlockComplete(&txn, ZX_OK, async_id);
        return;
      }
      break;
    }
    case BLOCK_OP_FLUSH:
      // queue the flush op. because there is no out of order execution in this
      // driver, when this op gets processed all previous ops are complete.
      break;
    default:
      BlockComplete(&txn, ZX_ERR_NOT_SUPPORTED, async_id);
      return;
  }

  fbl::AutoLock lock(&lock_);

  txn_list_.push(std::move(txn));
  // Wake up the worker thread.
  worker_event_.Broadcast();
}

int SdmmcBlockDevice::WorkerThread() {
  fbl::AutoLock lock(&lock_);

  for (;;) {
    if (dead_) {
      break;
    }

    for (std::optional<BlockOperation> txn = txn_list_.pop(); txn; txn = txn_list_.pop()) {
      DoTxn(&(*txn));
    }

    worker_event_.Wait(&lock_);
  }

  zxlogf(TRACE, "sdmmc: worker thread terminated successfully\n");
  return thrd_success;
}

zx_status_t SdmmcBlockDevice::WaitForTran() {
  uint32_t current_state;
  size_t attempt = 0;
  for (; attempt <= kTranMaxAttempts; attempt++) {
    uint32_t response;
    zx_status_t st = sdmmc_.SdmmcSendStatus(&response);
    if (st != ZX_OK) {
      zxlogf(ERROR, "sdmmc: SDMMC_SEND_STATUS error, retcode = %d\n", st);
      return st;
    }

    current_state = MMC_STATUS_CURRENT_STATE(response);
    if (current_state == MMC_STATUS_CURRENT_STATE_RECV) {
      st = sdmmc_.SdmmcStopTransmission();
      continue;
    } else if (current_state == MMC_STATUS_CURRENT_STATE_TRAN) {
      break;
    }

    zx::nanosleep(zx::deadline_after(zx::msec(10)));
  }

  if (attempt == kTranMaxAttempts) {
    // Too many retries, fail.
    return ZX_ERR_TIMED_OUT;
  } else {
    return ZX_OK;
  }
}

void SdmmcBlockDevice::SetBlockInfo(uint32_t block_size, uint64_t block_count) {
  block_info_.block_size = block_size;
  block_info_.block_count = block_count;
}

}  // namespace sdmmc
