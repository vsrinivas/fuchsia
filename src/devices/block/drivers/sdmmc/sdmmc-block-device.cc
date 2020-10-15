// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdmmc-block-device.h"

#include <inttypes.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/unowned_ptr.h>
#include <lib/fzl/vmo-mapper.h>
#include <zircon/hw/gpt.h>
#include <zircon/process.h>
#include <zircon/threads.h>

#include <ddk/debug.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>

namespace {

constexpr size_t kTranMaxAttempts = 10;

constexpr uint32_t kBlockOp(uint32_t op) { return op & BLOCK_OP_MASK; }

// Boot and RPMB partition sizes are in units of 128 KiB/KB.
constexpr uint32_t kBootSizeMultiplier = 128 * 1024;

inline void BlockComplete(sdmmc::BlockOperation& txn, zx_status_t status) {
  if (txn.node()->complete_cb()) {
    txn.Complete(status);
  } else {
    zxlogf(DEBUG, "sdmmc: block op %p completion_cb unset!", txn.operation());
  }
}

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
        memcpy(&out_guid->data1, kGuidEmmcBoot1Value, GUID_LENGTH);
      } else {
        memcpy(&out_guid->data1, kGuidEmmcBoot2Value, GUID_LENGTH);
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

zx_status_t RpmbDevice::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  llcpp::fuchsia::hardware::rpmb::Rpmb::Dispatch(this, msg, &transaction);
  return ZX_ERR_ASYNC;
}

void RpmbDevice::RpmbConnectServer(zx::channel server) {
  zx_status_t status;
  if (!loop_started_ && (status = loop_.StartThread("sdmmc-rpmb-thread")) != ZX_OK) {
    zxlogf(ERROR, "sdmmc: failed to start RPMB thread: %d", status);
  }

  loop_started_ = true;

  status = fidl::BindSingleInFlightOnly(loop_.dispatcher(), std::move(server), this);
  if (status != ZX_OK) {
    zxlogf(ERROR, "sdmmc: failed to bind channel: %d", status);
  }
}

void RpmbDevice::GetDeviceInfo(GetDeviceInfoCompleter::Sync& completer) {
  using DeviceInfo = ::llcpp::fuchsia::hardware::rpmb::DeviceInfo;
  using EmmcDeviceInfo = ::llcpp::fuchsia::hardware::rpmb::EmmcDeviceInfo;

  EmmcDeviceInfo emmc_info = {};
  memcpy(emmc_info.cid.data(), cid_.data(), cid_.size() * sizeof(cid_[0]));
  emmc_info.rpmb_size = rpmb_size_;
  emmc_info.reliable_write_sector_count = reliable_write_sector_count_;

  fidl::aligned<EmmcDeviceInfo> aligned_emmc_info(emmc_info);
  fidl::unowned_ptr_t<fidl::aligned<EmmcDeviceInfo>> emmc_info_ptr(&aligned_emmc_info);

  completer.ToAsync().Reply(DeviceInfo::WithEmmcInfo(emmc_info_ptr));
}

void RpmbDevice::Request(::llcpp::fuchsia::hardware::rpmb::Request request,
                         RequestCompleter::Sync& completer) {
  RpmbRequestInfo info = {
      .tx_frames = std::move(request.tx_frames),
      .completer = completer.ToAsync(),
  };

  if (request.rx_frames) {
    info.rx_frames = {
        .vmo = std::move(request.rx_frames->vmo),
        .offset = request.rx_frames->offset,
        .size = request.rx_frames->size,
    };
  }

  sdmmc_parent_->RpmbQueue(std::move(info));
}

zx_status_t SdmmcBlockDevice::Create(zx_device_t* parent, const SdmmcDevice& sdmmc,
                                     std::unique_ptr<SdmmcBlockDevice>* out_dev) {
  fbl::AllocChecker ac;
  out_dev->reset(new (&ac) SdmmcBlockDevice(parent, sdmmc));
  if (!ac.check()) {
    zxlogf(ERROR, "sdmmc: failed to allocate device memory");
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

zx_status_t SdmmcBlockDevice::AddDevice() {
  // Device must be in TRAN state at this point
  zx_status_t st = WaitForTran();
  if (st != ZX_OK) {
    zxlogf(ERROR, "sdmmc: waiting for TRAN state failed, retcode = %d", st);
    return ZX_ERR_TIMED_OUT;
  }

  fbl::AutoLock lock(&lock_);

  int rc = thrd_create_with_name(
      &worker_thread_,
      [](void* ctx) -> int { return reinterpret_cast<SdmmcBlockDevice*>(ctx)->WorkerThread(); },
      this, "sdmmc-block-worker");
  if (rc != thrd_success) {
    zxlogf(ERROR, "sdmmc: Failed to start worker thread, retcode = %d", st);
    return thrd_status_to_zx_status(rc);
  }

  st = DdkAdd(is_sd_ ? "sdmmc-sd" : "sdmmc-mmc", DEVICE_ADD_NON_BINDABLE);
  if (st != ZX_OK) {
    zxlogf(ERROR, "sdmmc: Failed to add block device, retcode = %d", st);
    return st;
  }

  fbl::AutoCall remove_device_on_error([&]() { DdkAsyncRemove(); });

  fbl::AllocChecker ac;
  std::unique_ptr<PartitionDevice> user_partition(
      new (&ac) PartitionDevice(zxdev(), this, block_info_, USER_DATA_PARTITION));
  if (!ac.check()) {
    zxlogf(ERROR, "sdmmc: failed to allocate device memory");
    return ZX_ERR_NO_MEMORY;
  }

  if ((st = user_partition->AddDevice()) != ZX_OK) {
    zxlogf(ERROR, "sdmmc: failed to add user partition device: %d", st);
    return st;
  }

  __UNUSED auto* dummy = user_partition.release();

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
      zxlogf(ERROR, "sdmmc: failed to allocate device memory");
      return ZX_ERR_NO_MEMORY;
    }

    std::unique_ptr<PartitionDevice> boot_partition_2(
        new (&ac) PartitionDevice(zxdev(), this, boot_info, BOOT_PARTITION_2));
    if (!ac.check()) {
      zxlogf(ERROR, "sdmmc: failed to allocate device memory");
      return ZX_ERR_NO_MEMORY;
    }

    if ((st = boot_partition_1->AddDevice()) != ZX_OK) {
      zxlogf(ERROR, "sdmmc: failed to add boot partition device: %d", st);
      return st;
    }

    dummy = boot_partition_1.release();

    if ((st = boot_partition_2->AddDevice()) != ZX_OK) {
      zxlogf(ERROR, "sdmmc: failed to add boot partition device: %d", st);
      return st;
    }

    dummy = boot_partition_2.release();
  }

  if (!is_sd_ && raw_ext_csd_[MMC_EXT_CSD_RPMB_SIZE_MULT] > 0) {
    std::unique_ptr<RpmbDevice> rpmb_partition(
        new (&ac) RpmbDevice(zxdev(), this, raw_cid_, raw_ext_csd_));
    if (!ac.check()) {
      zxlogf(ERROR, "sdmmc: failed to allocate device memory");
      return ZX_ERR_NO_MEMORY;
    }

    if ((st = rpmb_partition->DdkAdd("rpmb")) != ZX_OK) {
      zxlogf(ERROR, "sdmmc: failed to add RPMB partition device: %d", st);
      return st;
    }

    __UNUSED auto* dummy1 = rpmb_partition.release();
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

  sdmmc_req_t* req = &req_;
  memset(req, 0, sizeof(*req));
  req->cmd_idx = cmd_idx;
  req->cmd_flags = cmd_flags;
  req->arg = static_cast<uint32_t>(txn.offset_dev);
  req->blockcount = static_cast<uint16_t>(txn.length);
  req->blocksize = static_cast<uint16_t>(block_info_.block_size);

  // convert offset_vmo and length to bytes
  uint64_t offset_vmo = txn.offset_vmo * block_info_.block_size;
  uint64_t length = txn.length * block_info_.block_size;

  fzl::VmoMapper mapper;

  if (sdmmc_.UseDma()) {
    req->use_dma = true;
    req->virt_buffer = nullptr;
    req->pmt = ZX_HANDLE_INVALID;
    req->dma_vmo = txn.vmo;
    req->buf_offset = offset_vmo;
  } else {
    req->use_dma = false;
    st = mapper.Map(*zx::unowned_vmo(txn.vmo), offset_vmo, length,
                    ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
    if (st != ZX_OK) {
      zxlogf(DEBUG, "sdmmc: do_txn vmo map error %d", st);
      return st;
    }
    req->virt_buffer = mapper.start();
    req->virt_size = length;
  }

  st = sdmmc_.host().Request(req);

  if (st != ZX_OK || ((req->blockcount > 1) && !(req->cmd_flags & SDMMC_CMD_AUTO12))) {
    zx_status_t stop_st = sdmmc_.SdmmcStopTransmission();
    if (stop_st != ZX_OK) {
      zxlogf(DEBUG, "sdmmc: do_txn stop transmission error %d", stop_st);
    }
  }

  if (st != ZX_OK) {
    zxlogf(DEBUG, "sdmmc: do_txn error %d", st);
  }

  zxlogf(DEBUG, "sdmmc: do_txn complete");
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

  sdmmc_req_t discard_start = {
      .cmd_idx = MMC_ERASE_GROUP_START,
      .cmd_flags = MMC_ERASE_GROUP_START_FLAGS,
      .arg = static_cast<uint32_t>(txn.offset_dev),
  };
  if ((status = sdmmc_.host().Request(&discard_start)) != ZX_OK) {
    zxlogf(ERROR, "sdmmc: failed to set discard group start: %d", status);
    return status;
  }
  if (discard_start.response[0] & kEraseErrorFlags) {
    zxlogf(ERROR, "sdmmc: card reported discard group start error: 0x%08x",
           discard_start.response[0]);
    return ZX_ERR_IO;
  }

  sdmmc_req_t discard_end = {
      .cmd_idx = MMC_ERASE_GROUP_END,
      .cmd_flags = MMC_ERASE_GROUP_END_FLAGS,
      .arg = static_cast<uint32_t>(txn.offset_dev + txn.length - 1),
  };
  if ((status = sdmmc_.host().Request(&discard_end)) != ZX_OK) {
    zxlogf(ERROR, "sdmmc: failed to set discard group end: %d", status);
    return status;
  }
  if (discard_end.response[0] & kEraseErrorFlags) {
    zxlogf(ERROR, "sdmmc: card reported discard group end error: 0x%08x", discard_end.response[0]);
    return ZX_ERR_IO;
  }

  sdmmc_req_t discard = {
      .cmd_idx = SDMMC_ERASE,
      .cmd_flags = SDMMC_ERASE_FLAGS,
      .arg = MMC_ERASE_DISCARD_ARG,
  };
  if ((status = sdmmc_.host().Request(&discard)) != ZX_OK) {
    zxlogf(ERROR, "sdmmc: discard failed: %d", status);
    return status;
  }
  if (discard.response[0] & kEraseErrorFlags) {
    zxlogf(ERROR, "sdmmc: card reported discard error: 0x%08x", discard.response[0]);
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

zx_status_t SdmmcBlockDevice::RpmbRequest(const RpmbRequestInfo& request) {
  using ::llcpp::fuchsia::hardware::rpmb::FRAME_SIZE;

  const uint64_t tx_frame_count = request.tx_frames.size / FRAME_SIZE;
  const uint64_t rx_frame_count =
      request.rx_frames.vmo.is_valid() ? (request.rx_frames.size / FRAME_SIZE) : 0;
  const bool read_needed = rx_frame_count > 0;

  zx_status_t status = SetPartition(RPMB_PARTITION);
  if (status != ZX_OK) {
    return status;
  }

  fzl::VmoMapper tx_frames_mapper;
  fzl::VmoMapper rx_frames_mapper;
  if (!sdmmc_.UseDma()) {
    if ((status = tx_frames_mapper.Map(request.tx_frames.vmo, 0, 0, ZX_VM_PERM_READ)) != ZX_OK) {
      zxlogf(ERROR, "sdmmc: failed to map RPMB VMO: %d", status);
      return status;
    }

    if (read_needed) {
      status =
          rx_frames_mapper.Map(request.rx_frames.vmo, 0, 0, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
      if (status != ZX_OK) {
        zxlogf(ERROR, "sdmmc: failed to map RPMB VMO: %d", status);
        return status;
      }
    }
  }

  sdmmc_req_t set_tx_block_count = {
      .cmd_idx = SDMMC_SET_BLOCK_COUNT,
      .cmd_flags = SDMMC_SET_BLOCK_COUNT_FLAGS,
      .arg = MMC_SET_BLOCK_COUNT_RELIABLE_WRITE | static_cast<uint32_t>(tx_frame_count),
  };
  if ((status = sdmmc_.host().Request(&set_tx_block_count)) != ZX_OK) {
    zxlogf(ERROR, "sdmmc: failed to set block count for RPMB request: %d", status);
    return status;
  }

  sdmmc_req_t write_tx_frames = {
      .cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS,
      .arg = 0,  // Ignored by the card.
      .blockcount = static_cast<uint16_t>(tx_frame_count),
      .blocksize = FRAME_SIZE,
      .use_dma = sdmmc_.UseDma(),
      .dma_vmo = sdmmc_.UseDma() ? request.tx_frames.vmo.get() : ZX_HANDLE_INVALID,
      .virt_buffer = sdmmc_.UseDma() ? nullptr : tx_frames_mapper.start(),
      .buf_offset = request.tx_frames.offset,
  };
  if ((status = sdmmc_.host().Request(&write_tx_frames)) != ZX_OK) {
    zxlogf(ERROR, "sdmmc: failed to write RPMB frames: %d", status);
    return status;
  }

  if (!read_needed) {
    return ZX_OK;
  }

  sdmmc_req_t set_rx_block_count = {
      .cmd_idx = SDMMC_SET_BLOCK_COUNT,
      .cmd_flags = SDMMC_SET_BLOCK_COUNT_FLAGS,
      .arg = static_cast<uint32_t>(rx_frame_count),
  };
  if ((status = sdmmc_.host().Request(&set_rx_block_count)) != ZX_OK) {
    zxlogf(ERROR, "mmc: failed to set block count for RPMB request: %d", status);
    return status;
  }

  sdmmc_req_t read_rx_frames = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0,
      .blockcount = static_cast<uint16_t>(rx_frame_count),
      .blocksize = FRAME_SIZE,
      .use_dma = sdmmc_.UseDma(),
      .dma_vmo = sdmmc_.UseDma() ? request.rx_frames.vmo.get() : ZX_HANDLE_INVALID,
      .virt_buffer = sdmmc_.UseDma() ? nullptr : rx_frames_mapper.start(),
      .buf_offset = request.rx_frames.offset,
  };
  if ((status = sdmmc_.host().Request(&read_rx_frames)) != ZX_OK) {
    zxlogf(ERROR, "sdmmc: failed to read RPMB frames: %d", status);
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
    zxlogf(ERROR, "sdmmc: failed to switch to partition %u", partition);
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
  using ::llcpp::fuchsia::hardware::rpmb::FRAME_SIZE;

  if (info.tx_frames.size % FRAME_SIZE != 0) {
    zxlogf(ERROR, "sdmmc: tx frame buffer size not a multiple of %u", FRAME_SIZE);
    info.completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  // Checking against SDMMC_SET_BLOCK_COUNT_MAX_BLOCKS is sufficient for casting to uint16_t.
  static_assert(SDMMC_SET_BLOCK_COUNT_MAX_BLOCKS <= UINT16_MAX);

  const uint64_t tx_frame_count = info.tx_frames.size / FRAME_SIZE;
  if (tx_frame_count == 0) {
    info.completer.ReplyError(ZX_OK);
    return;
  }

  if (tx_frame_count > SDMMC_SET_BLOCK_COUNT_MAX_BLOCKS) {
    zxlogf(ERROR, "sdmmc: received %lu tx frames, maximum is %u", tx_frame_count,
           SDMMC_SET_BLOCK_COUNT_MAX_BLOCKS);
    info.completer.ReplyError(ZX_ERR_OUT_OF_RANGE);
    return;
  }

  if (info.rx_frames.vmo.is_valid()) {
    if (info.rx_frames.size % FRAME_SIZE != 0) {
      zxlogf(ERROR, "sdmmc: rx frame buffer size is not a multiple of %u", FRAME_SIZE);
      info.completer.ReplyError(ZX_ERR_INVALID_ARGS);
      return;
    }

    const uint64_t rx_frame_count = info.rx_frames.size / FRAME_SIZE;
    if (rx_frame_count > SDMMC_SET_BLOCK_COUNT_MAX_BLOCKS) {
      zxlogf(ERROR, "sdmmc: received %lu rx frames, maximum is %u", rx_frame_count,
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
      zxlogf(ERROR, "sdmmc: invalid block op %d", kBlockOp(btxn.operation()->command));
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
  for (;;) {
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

  zxlogf(DEBUG, "sdmmc: worker thread terminated successfully");
  return thrd_success;
}

zx_status_t SdmmcBlockDevice::WaitForTran() {
  uint32_t current_state;
  size_t attempt = 0;
  for (; attempt <= kTranMaxAttempts; attempt++) {
    uint32_t response;
    zx_status_t st = sdmmc_.SdmmcSendStatus(&response);
    if (st != ZX_OK) {
      zxlogf(ERROR, "sdmmc: SDMMC_SEND_STATUS error, retcode = %d", st);
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
