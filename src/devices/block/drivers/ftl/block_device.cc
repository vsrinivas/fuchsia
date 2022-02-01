// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block_device.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/trace/event.h>
#include <lib/fzl/vmo-mapper.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>
#include <zircon/status.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <iostream>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>

#include "lib/inspect/cpp/vmo/types.h"
#include "nand_driver.h"
#include "src/devices/block/drivers/ftl/metrics.h"

namespace {

constexpr char kDeviceName[] = "ftl";

// Encapsulates a block operation that is created by this device (so that it
// goes through the worker thread).
class LocalOperation {
 public:
  explicit LocalOperation(uint32_t command) { operation_.op.command = command; }

  block_op_t* op() { return &operation_.op; }

  // Waits for the completion of the operation. Returns the operation status.
  zx_status_t Execute(ftl::BlockDevice* parent) {
    parent->BlockImplQueue(&operation_.op, OnCompletion, this);
    zx_status_t status = sync_completion_wait(&event_, ZX_SEC(60));
    sync_completion_reset(&event_);
    if (status != ZX_OK) {
      return status;
    }
    return status_;
  }

 private:
  static void OnCompletion(void* cookie, zx_status_t status, block_op_t* op) {
    LocalOperation* operation = reinterpret_cast<LocalOperation*>(cookie);
    ZX_DEBUG_ASSERT(operation);
    operation->status_ = status;
    sync_completion_signal(&operation->event_);
  }

  sync_completion_t event_;
  zx_status_t status_ = ZX_ERR_BAD_STATE;
  ftl::FtlOp operation_ = {};
};

}  // namespace

namespace ftl {

BlockDevice::~BlockDevice() {
  if (thread_created_) {
    Kill();
    sync_completion_signal(&wake_signal_);
    int result_code;
    thrd_join(worker_, &result_code);
  }
  ZX_ASSERT(list_is_empty(&txn_list_));
  bool volume_created = (DdkGetSize() != 0);
  if (volume_created) {
    if (zx_status_t status = volume_->Unmount(); status != ZX_OK) {
      zxlogf(ERROR, "FTL: FtlUmount() failed: %s", zx_status_get_string(status));
    }
  }
}

zx_status_t BlockDevice::Bind() {
  zxlogf(INFO, "FTL: Binding to parent");

  if (device_get_protocol(parent(), ZX_PROTOCOL_NAND, &parent_) != ZX_OK) {
    zxlogf(ERROR, "FTL: Parent device does not support nand protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Get the optional bad block protocol.
  if (device_get_protocol(parent(), ZX_PROTOCOL_BAD_BLOCK, &bad_block_) != ZX_OK) {
    zxlogf(WARNING, "FTL: Parent device does not support bad_block protocol");
  }

  zx_status_t status = Init();
  if (status != ZX_OK) {
    return status;
  }
  return DdkAdd(ddk::DeviceAddArgs(kDeviceName).set_inspect_vmo(metrics_.DuplicateInspectVmo()));
}

void BlockDevice::DdkUnbind(ddk::UnbindTxn txn) {
  Kill();
  sync_completion_signal(&wake_signal_);
  txn.Reply();
}

zx_status_t BlockDevice::Init() {
  ZX_DEBUG_ASSERT(!thread_created_);
  if (thrd_create_with_name(&worker_, WorkerThreadStub, this, "ftl_worker") != thrd_success) {
    return ZX_ERR_NO_RESOURCES;
  }
  thread_created_ = true;

  // Set a scheduling deadline profile for the ftl_worker thread.
  // This is required in order to service the blobfs-pager-thread, which is on a deadline profile.
  // This will no longer be needed once we have the ability to propagate deadlines. Until then, we
  // need to set deadline profiles for all threads that the blobfs-pager-thread interacts with in
  // order to service page requests.
  //
  // TODO(fxbug.dev/40858): Migrate to the role-based API when available, instead of hard
  // coding parameters.
  const zx_duration_t capacity = ZX_USEC(400);
  const zx_duration_t deadline = ZX_MSEC(2);
  const zx_duration_t period = deadline;

  zx_handle_t profile = ZX_HANDLE_INVALID;
  zx_status_t status = device_get_deadline_profile(this->zxdev(), capacity, deadline, period,
                                                   "driver_host:pdev:05:00:f:ftl_worker", &profile);
  if (status != ZX_OK) {
    zxlogf(WARNING, "FTL: Failed to get deadline profile: %d\n", status);
  } else {
    const zx_handle_t thread_handle = thrd_get_zx_handle(worker_);
    status = zx_object_set_profile(thread_handle, profile, 0);
    if (status != ZX_OK) {
      zxlogf(WARNING, "FTL: Failed to set deadline profile: %d\n", status);
    }
    zx_handle_close(profile);
  }

  if (!InitFtl()) {
    return ZX_ERR_NO_RESOURCES;
  }

  return ZX_OK;
}

zx_status_t BlockDevice::Suspend() {
  LocalOperation operation(BLOCK_OP_FLUSH);
  return operation.Execute(this);
}

void BlockDevice::DdkSuspend(ddk::SuspendTxn txn) {
  zxlogf(INFO, "FTL: Suspend");
  zx_status_t status = Suspend();
  txn.Reply(status, txn.requested_state());
}

zx_status_t BlockDevice::DdkGetProtocol(uint32_t proto_id, void* out_protocol) {
  auto* proto = static_cast<ddk::AnyProtocol*>(out_protocol);
  proto->ctx = this;
  switch (proto_id) {
    case ZX_PROTOCOL_BLOCK_IMPL:
      proto->ops = &block_impl_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_BLOCK_PARTITION:
      proto->ops = &block_partition_protocol_ops_;
      return ZX_OK;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

void BlockDevice::BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out) {
  zxlogf(DEBUG, "FTL: Query");
  memset(info_out, 0, sizeof(*info_out));
  info_out->block_count = params_.num_pages;
  info_out->block_size = params_.page_size;
  info_out->flags = BLOCK_FLAG_TRIM_SUPPORT;
  info_out->max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED;
  *block_op_size_out = sizeof(FtlOp);
}

void BlockDevice::BlockImplQueue(block_op_t* operation, block_impl_queue_callback completion_cb,
                                 void* cookie) {
  zxlogf(DEBUG, "FTL: Queue");
  uint32_t max_pages = params_.num_pages;
  switch (operation->command) {
    case BLOCK_OP_WRITE:
    case BLOCK_OP_READ: {
      if (operation->rw.offset_dev >= max_pages || !operation->rw.length ||
          (max_pages - operation->rw.offset_dev) < operation->rw.length) {
        completion_cb(cookie, ZX_ERR_OUT_OF_RANGE, operation);
        return;
      }
      break;
    }
    case BLOCK_OP_TRIM:
      if (operation->trim.offset_dev >= max_pages || !operation->trim.length ||
          (max_pages - operation->trim.offset_dev) < operation->trim.length) {
        completion_cb(cookie, ZX_ERR_OUT_OF_RANGE, operation);
        return;
      }
      break;

    case BLOCK_OP_FLUSH:
      break;

    default:
      completion_cb(cookie, ZX_ERR_NOT_SUPPORTED, operation);
      return;
  }

  FtlOp* block_op = reinterpret_cast<FtlOp*>(operation);
  block_op->completion_cb = completion_cb;
  block_op->cookie = cookie;
  if (AddToList(block_op)) {
    sync_completion_signal(&wake_signal_);
  } else {
    completion_cb(cookie, ZX_ERR_BAD_STATE, operation);
  }
}

zx_status_t BlockDevice::BlockPartitionGetGuid(guidtype_t guid_type, guid_t* out_guid) {
  if (guid_type != GUIDTYPE_TYPE) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  memcpy(out_guid, guid_, ZBI_PARTITION_GUID_LEN);
  return ZX_OK;
}

zx_status_t BlockDevice::BlockPartitionGetName(char* out_name, size_t capacity) {
  if (capacity < sizeof(kDeviceName)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  strncpy(out_name, kDeviceName, capacity);
  return ZX_OK;
}

bool BlockDevice::OnVolumeAdded(uint32_t page_size, uint32_t num_pages) {
  params_ = {page_size, num_pages};
  zxlogf(INFO, "FTL: %d pages of %d bytes", num_pages, page_size);
  return true;
}

zx_status_t BlockDevice::FormatInternal() {
  zx_status_t status = volume_->Format();
  if (status != ZX_OK) {
    zxlogf(ERROR, "FTL: format failed: %s", zx_status_get_string(status));
  }
  return status;
}

bool BlockDevice::InitFtl() {
  std::unique_ptr<NandDriver> driver =
      NandDriver::CreateWithCounters(&parent_, &bad_block_, &nand_counters_);
  const char* error = driver->Init();
  if (error) {
    zxlogf(ERROR, "Failed to init FTL driver: %s", error);
    return false;
  }
  memcpy(guid_, driver->info().partition_guid, ZBI_PARTITION_GUID_LEN);

  if (!volume_) {
    volume_ = std::make_unique<ftl::VolumeImpl>(this);
  }

  error = volume_->Init(std::move(driver));
  if (error) {
    zxlogf(ERROR, "Failed to init FTL volume: %s", error);
    return false;
  }

  Volume::Stats stats;
  if (volume_->GetStats(&stats) == ZX_OK) {
    zxlogf(INFO, "FTL: Wear count: %u, Garbage level: %d%%", stats.wear_count, stats.garbage_level);
    metrics_.max_wear().Set(stats.wear_count);
    metrics_.initial_bad_blocks().Set(stats.initial_bad_blocks);
    metrics_.running_bad_blocks().Set(stats.running_bad_blocks);

    static_assert(std::size(stats.map_block_end_page_failure_reasons) == Metrics::kReasonCount);
    for (int i = 0; i < Metrics::kReasonCount; ++i) {
      metrics_.map_block_end_page_failure_reason(i).Set(
          stats.map_block_end_page_failure_reasons[i]);
    }
  }

  zxlogf(INFO, "FTL: InitFtl ok");
  return true;
}

void BlockDevice::Kill() {
  fbl::AutoLock lock(&lock_);
  dead_ = true;
}

bool BlockDevice::AddToList(FtlOp* operation) {
  fbl::AutoLock lock(&lock_);
  if (!dead_) {
    list_add_tail(&txn_list_, &operation->node);
  }
  return !dead_;
}

bool BlockDevice::RemoveFromList(FtlOp** operation) {
  fbl::AutoLock lock(&lock_);
  *operation = list_remove_head_type(&txn_list_, FtlOp, node);
  return !dead_;
}

int BlockDevice::WorkerThread() {
  for (;;) {
    FtlOp* operation;
    for (;;) {
      bool alive = RemoveFromList(&operation);
      if (operation) {
        if (alive) {
          sync_completion_reset(&wake_signal_);
          break;
        } else {
          operation->completion_cb(operation->cookie, ZX_ERR_BAD_STATE, &operation->op);
        }
      } else if (alive) {
        // Flush any pending data after 15 seconds of inactivity. This is
        // meant to reduce the chances of data loss if power is removed.
        // This value is only a guess.
        zx_duration_t timeout = pending_flush_ ? ZX_SEC(15) : ZX_TIME_INFINITE;
        zx_status_t status = sync_completion_wait(&wake_signal_, timeout);
        if (status == ZX_ERR_TIMED_OUT) {
          Flush();
          pending_flush_ = false;
        }
      } else {
        return 0;
      }
    }

    zx_status_t status = ZX_OK;

    // These counters are updated by the NdmDriver implementation, which will keep track of the
    // number of operation issued, during the context of this block operation, to the nand driver.
    //
    // The counters are reset before each block operation, so the numbers reflect the number of nand
    // operations issued as a result of this operation alone.
    //
    // These operations are then aggregated into the respective |BlockOperationProperties| of the
    // given block operation type.
    nand_counters_.Reset();
    ftl::BlockOperationProperties* op_stats = nullptr;
    {
      TRACE_DURATION_BEGIN("block:ftl", "Operation", "opcode", operation->op.command, "offset_dev",
                           operation->op.rw.offset_dev, "length", operation->op.rw.length);
      switch (operation->op.command) {
        case BLOCK_OP_WRITE:
          pending_flush_ = true;
          status = ReadWriteData(&operation->op);
          op_stats = &metrics_.write();
          break;

        case BLOCK_OP_READ:
          pending_flush_ = true;
          status = ReadWriteData(&operation->op);
          op_stats = &metrics_.read();
          break;

        case BLOCK_OP_TRIM:
          pending_flush_ = true;
          status = TrimData(&operation->op);
          op_stats = &metrics_.trim();
          break;

        case BLOCK_OP_FLUSH: {
          status = Flush();
          pending_flush_ = false;
          op_stats = &metrics_.flush();
          break;
        }
        default:
          ZX_DEBUG_ASSERT(false);  // Unexpected.
      }
      TRACE_DURATION_END("block:ftl", "Operation", "nand_ops", nand_counters_.GetSum());
    }

    Volume::Counters counters;
    if (volume_->GetCounters(&counters) == ZX_OK) {
      metrics_.max_wear().Set(counters.wear_count);
      metrics_.initial_bad_blocks().Set(counters.initial_bad_blocks);
      metrics_.running_bad_blocks().Set(counters.running_bad_blocks);
    }

    // Update all counters and rates for the supported operation type.
    if (op_stats != nullptr) {
      op_stats->count.Add(1);
      op_stats->all.count.Add(nand_counters_.GetSum());
      op_stats->all.rate.Add(nand_counters_.GetSum());
      op_stats->block_erase.count.Add(nand_counters_.block_erase);
      op_stats->block_erase.rate.Add(nand_counters_.block_erase);
      op_stats->page_write.count.Add(nand_counters_.page_write);
      op_stats->page_write.rate.Add(nand_counters_.page_write);
      op_stats->page_read.count.Add(nand_counters_.page_read);
      op_stats->page_read.rate.Add(nand_counters_.page_read);
    }
    operation->completion_cb(operation->cookie, status, &operation->op);
  }
}

int BlockDevice::WorkerThreadStub(void* arg) {
  BlockDevice* device = reinterpret_cast<BlockDevice*>(arg);
  return device->WorkerThread();
}

zx_status_t BlockDevice::ReadWriteData(block_op_t* operation) {
  uint64_t addr = operation->rw.offset_vmo * params_.page_size;
  uint32_t length = operation->rw.length * params_.page_size;
  uint32_t offset = static_cast<uint32_t>(operation->rw.offset_dev);
  if (offset != operation->rw.offset_dev) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // TODO(fxbug.dev/32393): We may go back to ask the kernel to copy the data for us
  // if that ends up being more efficient.
  fzl::VmoMapper mapper;
  zx_status_t status = mapper.Map(*zx::unowned_vmo(operation->rw.vmo), addr, length,
                                  ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE);
  if (status != ZX_OK) {
    return status;
  }

  if (operation->command == BLOCK_OP_WRITE) {
    zxlogf(TRACE, "FTL: BLK To write %d blocks at %d :", operation->rw.length, offset);
    status = volume_->Write(offset, operation->rw.length, mapper.start());
    if (status != ZX_OK) {
      zxlogf(ERROR, "FTL: Failed to write %u@%u: %s", operation->rw.length, offset,
             zx_status_get_string(status));
      return status;
    }
  }

  if (operation->command == BLOCK_OP_READ) {
    zxlogf(TRACE, "FTL: BLK To read %d blocks at %d :", operation->rw.length, offset);
    status = volume_->Read(offset, operation->rw.length, mapper.start());
    if (status != ZX_OK) {
      zxlogf(ERROR, "FTL: Failed to read %u@%u: %s", operation->rw.length, offset,
             zx_status_get_string(status));
      return status;
    }
  }

  return ZX_OK;
}

zx_status_t BlockDevice::TrimData(block_op_t* operation) {
  uint32_t offset = static_cast<uint32_t>(operation->trim.offset_dev);
  if (offset != operation->trim.offset_dev) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  ZX_DEBUG_ASSERT(operation->command == BLOCK_OP_TRIM);
  zxlogf(TRACE, "FTL: BLK To trim %d blocks at %d :", operation->trim.length, offset);
  zx_status_t status = volume_->Trim(offset, operation->trim.length);
  if (status != ZX_OK) {
    zxlogf(ERROR, "FTL: Failed to trim: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

zx_status_t BlockDevice::Flush() {
  zx_status_t status = volume_->Flush();
  if (status != ZX_OK) {
    zxlogf(ERROR, "FTL: flush failed: %s", zx_status_get_string(status));
    return status;
  }

  zxlogf(TRACE, "FTL: Finished flush");
  return status;
}

}  // namespace ftl.
