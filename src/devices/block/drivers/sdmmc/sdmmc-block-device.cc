// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdmmc-block-device.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fit/defer.h>
#include <lib/fzl/vmo-mapper.h>
#include <threads.h>
#include <zircon/hw/gpt.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/threads.h>

#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>

#include "sdmmc-partition-device.h"
#include "sdmmc-rpmb-device.h"

namespace {

constexpr size_t kTranMaxAttempts = 10;

constexpr uint32_t kBlockOp(uint32_t op) { return op & BLOCK_OP_MASK; }

// Boot and RPMB partition sizes are in units of 128 KiB/KB.
constexpr uint32_t kBootSizeMultiplier = 128 * 1024;

inline void BlockComplete(sdmmc::BlockOperation& txn, zx_status_t status) {
  if (txn.node()->complete_cb()) {
    txn.Complete(status);
  } else {
    zxlogf(DEBUG, "block op %p completion_cb unset!", txn.operation());
  }
}

}  // namespace

namespace sdmmc {

zx_status_t SdmmcBlockDevice::Create(zx_device_t* parent, const SdmmcDevice& sdmmc,
                                     std::unique_ptr<SdmmcBlockDevice>* out_dev) {
  fbl::AllocChecker ac;
  out_dev->reset(new (&ac) SdmmcBlockDevice(parent, sdmmc));
  if (!ac.check()) {
    zxlogf(ERROR, "failed to allocate device memory");
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

zx_status_t SdmmcBlockDevice::AddDevice() {
  // Device must be in TRAN state at this point
  zx_status_t st = WaitForTran();
  if (st != ZX_OK) {
    zxlogf(ERROR, "waiting for TRAN state failed, retcode = %d", st);
    return ZX_ERR_TIMED_OUT;
  }

  root_ = inspector_.GetRoot().CreateChild("sdmmc_core");
  io_errors_ = root_.CreateUint("io_errors", 0);
  io_retries_ = root_.CreateUint("io_retries", 0);

  if (!is_sd_) {
    MmcSetInspectProperties();
  }

  fbl::AutoLock lock(&lock_);

  int rc = thrd_create_with_name(
      &worker_thread_,
      [](void* ctx) -> int { return reinterpret_cast<SdmmcBlockDevice*>(ctx)->WorkerThread(); },
      this, "sdmmc-block-worker");
  if (rc != thrd_success) {
    zxlogf(ERROR, "Failed to start worker thread, retcode = %d", st);
    return thrd_status_to_zx_status(rc);
  }

  st = DdkAdd(ddk::DeviceAddArgs(is_sd_ ? "sdmmc-sd" : "sdmmc-mmc")
                  .set_flags(DEVICE_ADD_NON_BINDABLE)
                  .set_inspect_vmo(inspector_.DuplicateVmo()));
  if (st != ZX_OK) {
    zxlogf(ERROR, "Failed to add block device, retcode = %d", st);
    return st;
  }

  auto remove_device_on_error = fit::defer([&]() { DdkAsyncRemove(); });

  fbl::AllocChecker ac;
  std::unique_ptr<PartitionDevice> user_partition(
      new (&ac) PartitionDevice(zxdev(), this, block_info_, USER_DATA_PARTITION));
  if (!ac.check()) {
    zxlogf(ERROR, "failed to allocate device memory");
    return ZX_ERR_NO_MEMORY;
  }

  if ((st = user_partition->AddDevice()) != ZX_OK) {
    zxlogf(ERROR, "failed to add user partition device: %d", st);
    return st;
  }

  __UNUSED auto* placeholder = user_partition.release();

  const uint32_t boot_size = raw_ext_csd_[MMC_EXT_CSD_BOOT_SIZE_MULT] * kBootSizeMultiplier;
  const bool boot_enabled =
      raw_ext_csd_[MMC_EXT_CSD_PARTITION_CONFIG] & MMC_EXT_CSD_BOOT_PARTITION_ENABLE_MASK;

  const uint64_t boot_partition_block_count = boot_size / block_info_.block_size;
  const block_info_t boot_info = {
      .block_count = boot_partition_block_count,
      .block_size = block_info_.block_size,
      .max_transfer_size = block_info_.max_transfer_size,
      .flags = block_info_.flags,
      .reserved = 0,
  };

  if (!is_sd_ && boot_size > 0 && boot_enabled) {
    std::unique_ptr<PartitionDevice> boot_partition_1(
        new (&ac) PartitionDevice(zxdev(), this, boot_info, BOOT_PARTITION_1));
    if (!ac.check()) {
      zxlogf(ERROR, "failed to allocate device memory");
      return ZX_ERR_NO_MEMORY;
    }

    std::unique_ptr<PartitionDevice> boot_partition_2(
        new (&ac) PartitionDevice(zxdev(), this, boot_info, BOOT_PARTITION_2));
    if (!ac.check()) {
      zxlogf(ERROR, "failed to allocate device memory");
      return ZX_ERR_NO_MEMORY;
    }

    if ((st = boot_partition_1->AddDevice()) != ZX_OK) {
      zxlogf(ERROR, "failed to add boot partition device: %d", st);
      return st;
    }

    placeholder = boot_partition_1.release();

    if ((st = boot_partition_2->AddDevice()) != ZX_OK) {
      zxlogf(ERROR, "failed to add boot partition device: %d", st);
      return st;
    }

    placeholder = boot_partition_2.release();
  }

  if (!is_sd_ && raw_ext_csd_[MMC_EXT_CSD_RPMB_SIZE_MULT] > 0) {
    st = RpmbDevice::Create(zxdev(), this, raw_cid_, raw_ext_csd_);
    if (st != ZX_OK) {
      return st;
    }
  }

  remove_device_on_error.cancel();
  return ZX_OK;
}

void SdmmcBlockDevice::DdkUnbind(ddk::UnbindTxn txn) {
  StopWorkerThread();
  txn.Reply();
}

void SdmmcBlockDevice::DdkSuspend(ddk::SuspendTxn txn) {
  StopWorkerThread();
  txn.Reply(ZX_OK, txn.requested_state());
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
  }

  // error out all pending requests
  fbl::AutoLock lock(&lock_);
  for (std::optional<BlockOperation> txn = txn_list_.pop(); txn; txn = txn_list_.pop()) {
    BlockComplete(*txn, ZX_ERR_BAD_STATE);
  }

  for (auto& request : rpmb_list_) {
    request.completer.ReplyError(ZX_ERR_BAD_STATE);
  }
  rpmb_list_.clear();
}

zx_status_t SdmmcBlockDevice::ReadWrite(const block_read_write_t& txn,
                                        const EmmcPartition partition) {
  zx_status_t st = SetPartition(partition);
  if (st != ZX_OK) {
    return st;
  }

  uint32_t cmd_idx = 0;
  uint32_t cmd_flags = 0;

  if (kBlockOp(txn.command) == BLOCK_OP_READ) {
    if (txn.length > 1) {
      cmd_idx = SDMMC_READ_MULTIPLE_BLOCK;
      cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS;
      if (sdmmc_.host_info().caps & SDMMC_HOST_CAP_AUTO_CMD12) {
        cmd_flags |= SDMMC_CMD_AUTO12;
      }
    } else {
      cmd_idx = SDMMC_READ_BLOCK;
      cmd_flags = SDMMC_READ_BLOCK_FLAGS;
    }
  } else {
    if (txn.length > 1) {
      cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK;
      cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS;
      if (sdmmc_.host_info().caps & SDMMC_HOST_CAP_AUTO_CMD12) {
        cmd_flags |= SDMMC_CMD_AUTO12;
      }
    } else {
      cmd_idx = SDMMC_WRITE_BLOCK;
      cmd_flags = SDMMC_WRITE_BLOCK_FLAGS;
    }
  }

  zxlogf(DEBUG,
         "sdmmc: do_txn blockop 0x%x offset_vmo 0x%" PRIx64
         " length 0x%x blocksize 0x%x"
         " max_transfer_size 0x%x",
         txn.command, txn.offset_vmo, txn.length, block_info_.block_size,
         block_info_.max_transfer_size);

  // convert offset_vmo and length to bytes
  const uint64_t offset_vmo = txn.offset_vmo * block_info_.block_size;
  const uint64_t length = txn.length * block_info_.block_size;

  const sdmmc_buffer_region_t region = {
      .buffer = {.vmo = txn.vmo},
      .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
      .offset = offset_vmo,
      .size = length,
  };
  const sdmmc_req_new_t req = {
      .cmd_idx = cmd_idx,
      .cmd_flags = cmd_flags,
      .arg = static_cast<uint32_t>(txn.offset_dev),
      .blocksize = block_info_.block_size,
      .buffers_list = &region,
      .buffers_count = 1,
  };

  uint32_t retries = 0;
  st = sdmmc_.SdmmcIoRequestWithRetries(req, &retries);
  io_retries_.Add(retries);
  if (st != ZX_OK) {
    zxlogf(ERROR, "do_txn error %d", st);
    io_errors_.Add(1);
  }

  // SdmmcIoRequestWithRetries sends STOP_TRANSMISSION (cmd12) when an error occurs, so it only
  // needs to be sent here if the request succeeded, there was more than one block, and the
  // controller doesn't support auto cmd12.
  if (st == ZX_OK && txn.length > 1 && !(req.cmd_flags & SDMMC_CMD_AUTO12)) {
    zx_status_t stop_st = sdmmc_.SdmmcStopTransmission();
    if (stop_st != ZX_OK) {
      zxlogf(WARNING, "do_txn stop transmission error %d", stop_st);
      io_errors_.Add(1);
    }
  }

  zxlogf(DEBUG, "do_txn complete");
  return st;
}

zx_status_t SdmmcBlockDevice::Trim(const block_trim_t& txn, const EmmcPartition partition) {
  // TODO(bradenkell): Add discard support for SD.
  if (is_sd_) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (!(block_info_.flags & BLOCK_FLAG_TRIM_SUPPORT)) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status = SetPartition(partition);
  if (status != ZX_OK) {
    return status;
  }

  constexpr uint32_t kEraseErrorFlags =
      MMC_STATUS_ADDR_OUT_OF_RANGE | MMC_STATUS_ERASE_SEQ_ERR | MMC_STATUS_ERASE_PARAM;

  const sdmmc_req_new_t discard_start = {
      .cmd_idx = MMC_ERASE_GROUP_START,
      .cmd_flags = MMC_ERASE_GROUP_START_FLAGS,
      .arg = static_cast<uint32_t>(txn.offset_dev),
  };
  uint32_t response[4] = {};
  if ((status = sdmmc_.host().RequestNew(&discard_start, response)) != ZX_OK) {
    zxlogf(ERROR, "failed to set discard group start: %d", status);
    io_errors_.Add(1);
    return status;
  }
  if (response[0] & kEraseErrorFlags) {
    zxlogf(ERROR, "card reported discard group start error: 0x%08x", response[0]);
    io_errors_.Add(1);
    return ZX_ERR_IO;
  }

  const sdmmc_req_new_t discard_end = {
      .cmd_idx = MMC_ERASE_GROUP_END,
      .cmd_flags = MMC_ERASE_GROUP_END_FLAGS,
      .arg = static_cast<uint32_t>(txn.offset_dev + txn.length - 1),
  };
  if ((status = sdmmc_.host().RequestNew(&discard_end, response)) != ZX_OK) {
    zxlogf(ERROR, "failed to set discard group end: %d", status);
    io_errors_.Add(1);
    return status;
  }
  if (response[0] & kEraseErrorFlags) {
    zxlogf(ERROR, "card reported discard group end error: 0x%08x", response[0]);
    io_errors_.Add(1);
    return ZX_ERR_IO;
  }

  const sdmmc_req_new_t discard = {
      .cmd_idx = SDMMC_ERASE,
      .cmd_flags = SDMMC_ERASE_FLAGS,
      .arg = MMC_ERASE_DISCARD_ARG,
  };
  if ((status = sdmmc_.host().RequestNew(&discard, response)) != ZX_OK) {
    zxlogf(ERROR, "discard failed: %d", status);
    io_errors_.Add(1);
    return status;
  }
  if (response[0] & kEraseErrorFlags) {
    zxlogf(ERROR, "card reported discard error: 0x%08x", response[0]);
    io_errors_.Add(1);
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

zx_status_t SdmmcBlockDevice::RpmbRequest(const RpmbRequestInfo& request) {
  // TODO(fxbug.dev/85455): Find out if RPMB requests can be retried.
  using fuchsia_hardware_rpmb::wire::kFrameSize;

  const uint64_t tx_frame_count = request.tx_frames.size / kFrameSize;
  const uint64_t rx_frame_count =
      request.rx_frames.vmo.is_valid() ? (request.rx_frames.size / kFrameSize) : 0;
  const bool read_needed = rx_frame_count > 0;

  zx_status_t status = SetPartition(RPMB_PARTITION);
  if (status != ZX_OK) {
    return status;
  }

  const sdmmc_req_new_t set_tx_block_count = {
      .cmd_idx = SDMMC_SET_BLOCK_COUNT,
      .cmd_flags = SDMMC_SET_BLOCK_COUNT_FLAGS,
      .arg = MMC_SET_BLOCK_COUNT_RELIABLE_WRITE | static_cast<uint32_t>(tx_frame_count),
  };
  uint32_t unused_response[4];
  if ((status = sdmmc_.host().RequestNew(&set_tx_block_count, unused_response)) != ZX_OK) {
    zxlogf(ERROR, "failed to set block count for RPMB request: %d", status);
    io_errors_.Add(1);
    return status;
  }

  const sdmmc_buffer_region_t write_region = {
      .buffer = {.vmo = request.tx_frames.vmo.get()},
      .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
      .offset = request.tx_frames.offset,
      .size = tx_frame_count * kFrameSize,
  };
  const sdmmc_req_new_t write_tx_frames = {
      .cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS,
      .arg = 0,  // Ignored by the card.
      .blocksize = kFrameSize,
      .buffers_list = &write_region,
      .buffers_count = 1,
  };
  if ((status = sdmmc_.host().RequestNew(&write_tx_frames, unused_response)) != ZX_OK) {
    zxlogf(ERROR, "failed to write RPMB frames: %d", status);
    io_errors_.Add(1);
    return status;
  }

  if (!read_needed) {
    return ZX_OK;
  }

  const sdmmc_req_new_t set_rx_block_count = {
      .cmd_idx = SDMMC_SET_BLOCK_COUNT,
      .cmd_flags = SDMMC_SET_BLOCK_COUNT_FLAGS,
      .arg = static_cast<uint32_t>(rx_frame_count),
  };
  if ((status = sdmmc_.host().RequestNew(&set_rx_block_count, unused_response)) != ZX_OK) {
    zxlogf(ERROR, "failed to set block count for RPMB request: %d", status);
    io_errors_.Add(1);
    return status;
  }

  const sdmmc_buffer_region_t read_region = {
      .buffer = {.vmo = request.rx_frames.vmo.get()},
      .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
      .offset = request.rx_frames.offset,
      .size = rx_frame_count * kFrameSize,
  };
  const sdmmc_req_new_t read_rx_frames = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0,
      .blocksize = kFrameSize,
      .buffers_list = &read_region,
      .buffers_count = 1,
  };
  if ((status = sdmmc_.host().RequestNew(&read_rx_frames, unused_response)) != ZX_OK) {
    zxlogf(ERROR, "failed to read RPMB frames: %d", status);
    io_errors_.Add(1);
    return status;
  }

  return ZX_OK;
}

zx_status_t SdmmcBlockDevice::SetPartition(const EmmcPartition partition) {
  // SetPartition is only called by the worker thread.
  static EmmcPartition current_partition = EmmcPartition::USER_DATA_PARTITION;

  if (is_sd_ || partition == current_partition) {
    return ZX_OK;
  }

  const uint8_t partition_config_value =
      (raw_ext_csd_[MMC_EXT_CSD_PARTITION_CONFIG] & MMC_EXT_CSD_PARTITION_ACCESS_MASK) | partition;

  zx_status_t status = MmcDoSwitch(MMC_EXT_CSD_PARTITION_CONFIG, partition_config_value);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to switch to partition %u", partition);
    io_errors_.Add(1);
    return status;
  }

  current_partition = partition;
  return ZX_OK;
}

void SdmmcBlockDevice::Queue(BlockOperation txn) {
  block_op_t* btxn = txn.operation();

  const uint64_t max = txn.private_storage()->block_count;
  switch (kBlockOp(btxn->command)) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
      if ((btxn->rw.offset_dev >= max) || ((max - btxn->rw.offset_dev) < btxn->rw.length)) {
        BlockComplete(txn, ZX_ERR_OUT_OF_RANGE);
        return;
      }
      if (btxn->rw.length == 0) {
        BlockComplete(txn, ZX_OK);
        return;
      }
      break;
    case BLOCK_OP_TRIM:
      if ((btxn->trim.offset_dev >= max) || ((max - btxn->trim.offset_dev) < btxn->trim.length)) {
        BlockComplete(txn, ZX_ERR_OUT_OF_RANGE);
        return;
      }
      if (btxn->trim.length == 0) {
        BlockComplete(txn, ZX_OK);
        return;
      }
      break;
    case BLOCK_OP_FLUSH:
      // queue the flush op. because there is no out of order execution in this
      // driver, when this op gets processed all previous ops are complete.
      break;
    default:
      BlockComplete(txn, ZX_ERR_NOT_SUPPORTED);
      return;
  }

  fbl::AutoLock lock(&lock_);

  txn_list_.push(std::move(txn));
  // Wake up the worker thread.
  worker_event_.Broadcast();
}

void SdmmcBlockDevice::RpmbQueue(RpmbRequestInfo info) {
  using fuchsia_hardware_rpmb::wire::kFrameSize;

  if (info.tx_frames.size % kFrameSize != 0) {
    zxlogf(ERROR, "tx frame buffer size not a multiple of %u", kFrameSize);
    info.completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  // Checking against SDMMC_SET_BLOCK_COUNT_MAX_BLOCKS is sufficient for casting to uint16_t.
  static_assert(SDMMC_SET_BLOCK_COUNT_MAX_BLOCKS <= UINT16_MAX);

  const uint64_t tx_frame_count = info.tx_frames.size / kFrameSize;
  if (tx_frame_count == 0) {
    info.completer.ReplyError(ZX_OK);
    return;
  }

  if (tx_frame_count > SDMMC_SET_BLOCK_COUNT_MAX_BLOCKS) {
    zxlogf(ERROR, "received %lu tx frames, maximum is %u", tx_frame_count,
           SDMMC_SET_BLOCK_COUNT_MAX_BLOCKS);
    info.completer.ReplyError(ZX_ERR_OUT_OF_RANGE);
    return;
  }

  if (info.rx_frames.vmo.is_valid()) {
    if (info.rx_frames.size % kFrameSize != 0) {
      zxlogf(ERROR, "rx frame buffer size is not a multiple of %u", kFrameSize);
      info.completer.ReplyError(ZX_ERR_INVALID_ARGS);
      return;
    }

    const uint64_t rx_frame_count = info.rx_frames.size / kFrameSize;
    if (rx_frame_count > SDMMC_SET_BLOCK_COUNT_MAX_BLOCKS) {
      zxlogf(ERROR, "received %lu rx frames, maximum is %u", rx_frame_count,
             SDMMC_SET_BLOCK_COUNT_MAX_BLOCKS);
      info.completer.ReplyError(ZX_ERR_OUT_OF_RANGE);
      return;
    }
  }

  fbl::AutoLock lock(&lock_);
  if (rpmb_list_.size() >= kMaxOutstandingRpmbRequests) {
    info.completer.ReplyError(ZX_ERR_SHOULD_WAIT);
  } else {
    rpmb_list_.push_back(std::move(info));
    worker_event_.Broadcast();
  }
}

void SdmmcBlockDevice::HandleBlockOps(block::BorrowedOperationQueue<PartitionInfo>& txn_list) {
  for (size_t i = 0; i < kRoundRobinRequestCount; i++) {
    std::optional<BlockOperation> txn = txn_list.pop();
    if (!txn) {
      break;
    }

    BlockOperation btxn(*std::move(txn));

    const block_op_t& bop = *btxn.operation();
    const uint32_t op = kBlockOp(bop.command);

    zx_status_t status = ZX_ERR_INVALID_ARGS;
    if (op == BLOCK_OP_READ || op == BLOCK_OP_WRITE) {
      const char* const trace_name = op == BLOCK_OP_READ ? "read" : "write";
      TRACE_DURATION_BEGIN("sdmmc", trace_name);

      status = ReadWrite(btxn.operation()->rw, btxn.private_storage()->partition);

      TRACE_DURATION_END("sdmmc", trace_name, "command", TA_INT32(bop.rw.command), "extra",
                         TA_INT32(bop.rw.extra), "length", TA_INT32(bop.rw.length), "offset_vmo",
                         TA_INT64(bop.rw.offset_vmo), "offset_dev", TA_INT64(bop.rw.offset_dev),
                         "txn_status", TA_INT32(status));
    } else if (op == BLOCK_OP_TRIM) {
      TRACE_DURATION_BEGIN("sdmmc", "trim");

      status = Trim(btxn.operation()->trim, btxn.private_storage()->partition);

      TRACE_DURATION_END("sdmmc", "trim", "command", TA_INT32(bop.trim.command), "length",
                         TA_INT32(bop.trim.length), "offset_dev", TA_INT64(bop.trim.offset_dev),
                         "txn_status", TA_INT32(status));
    } else if (op == BLOCK_OP_FLUSH) {
      status = ZX_OK;
      TRACE_INSTANT("sdmmc", "flush", TRACE_SCOPE_PROCESS, "command", TA_INT32(bop.rw.command),
                    "txn_status", TA_INT32(status));
    } else {
      // should not get here
      zxlogf(ERROR, "invalid block op %d", kBlockOp(btxn.operation()->command));
      TRACE_INSTANT("sdmmc", "unknown", TRACE_SCOPE_PROCESS, "command", TA_INT32(bop.rw.command),
                    "txn_status", TA_INT32(status));
      __UNREACHABLE;
    }

    BlockComplete(btxn, status);
  }
}

void SdmmcBlockDevice::HandleRpmbRequests(std::deque<RpmbRequestInfo>& rpmb_list) {
  for (size_t i = 0; i < kRoundRobinRequestCount && !rpmb_list.empty(); i++) {
    RpmbRequestInfo& request = *rpmb_list.begin();
    zx_status_t status = RpmbRequest(request);
    if (status == ZX_OK) {
      request.completer.ReplySuccess();
    } else {
      request.completer.ReplyError(status);
    }

    rpmb_list.pop_front();
  }
}

int SdmmcBlockDevice::WorkerThread() {
  {
    const char* role_name = "fuchsia.devices.block.drivers.sdmmc.worker";
    const size_t role_name_size = strlen(role_name);
    const zx_status_t status = device_set_profile_by_role(
        this->zxdev(), thrd_get_zx_handle(thrd_current()), role_name, role_name_size);
    if (status != ZX_OK) {
      zxlogf(WARNING,
             "Failed to apply role \"%s\" to worker thread: %s Performance may be reduced.",
             role_name, zx_status_get_string(status));
    }
  }

  for (;;) {
    TRACE_DURATION("sdmmc", "work loop");

    block::BorrowedOperationQueue<PartitionInfo> txn_list;
    std::deque<RpmbRequestInfo> rpmb_list;

    {
      fbl::AutoLock lock(&lock_);
      while (txn_list_.is_empty() && rpmb_list_.empty() && !dead_) {
        worker_event_.Wait(&lock_);
      }

      if (dead_) {
        break;
      }

      txn_list = std::move(txn_list_);
      rpmb_list.swap(rpmb_list_);
    }

    while (!txn_list.is_empty() || !rpmb_list.empty()) {
      HandleBlockOps(txn_list);
      HandleRpmbRequests(rpmb_list);
    }
  }

  zxlogf(DEBUG, "worker thread terminated successfully");
  return thrd_success;
}

zx_status_t SdmmcBlockDevice::WaitForTran() {
  uint32_t current_state;
  size_t attempt = 0;
  for (; attempt <= kTranMaxAttempts; attempt++) {
    uint32_t response;
    zx_status_t st = sdmmc_.SdmmcSendStatus(&response);
    if (st != ZX_OK) {
      zxlogf(ERROR, "SDMMC_SEND_STATUS error, retcode = %d", st);
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
