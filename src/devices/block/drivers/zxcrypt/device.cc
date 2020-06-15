// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <inttypes.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls/port.h>

#include <algorithm>
#include <utility>

#include <ddk/debug.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "debug.h"
#include "device-info.h"

namespace zxcrypt {

Device::Device(zx_device_t* parent, DeviceInfo&& info)
    : DeviceType(parent),
      active_(false),
      stalled_(false),
      num_ops_(0),
      info_(std::move(info)),
      hint_(0) {
  LOG_ENTRY();

  list_initialize(&queue_);
}

Device::~Device() { LOG_ENTRY(); }

zx_status_t Device::Init(const DdkVolume& volume) {
  LOG_ENTRY();
  zx_status_t rc;
  fbl::AutoLock lock(&mtx_);

  // Set up allocation bitmap
  if ((rc = map_.Reset(Volume::kBufferSize / info_.block_size)) != ZX_OK) {
    zxlogf(ERROR, "bitmap allocation failed: %s", zx_status_get_string(rc));
    return rc;
  }

  // Start workers
  if ((rc = zx::port::create(0, &port_)) != ZX_OK) {
    zxlogf(ERROR, "zx::port::create failed: %s", zx_status_get_string(rc));
    return rc;
  }
  for (size_t i = 0; i < kNumWorkers; ++i) {
    zx::port port;
    port_.duplicate(ZX_RIGHT_SAME_RIGHTS, &port);
    if ((rc = workers_[i].Start(this, volume, std::move(port))) != ZX_OK) {
      zxlogf(ERROR, "failed to start worker %zu: %s", i, zx_status_get_string(rc));
      return rc;
    }
  }

  // Enable the device.
  active_.store(true);
  return ZX_OK;
}

////////////////////////////////////////////////////////////////
// ddk::Device methods

zx_status_t Device::DdkGetProtocol(uint32_t proto_id, void* out) {
  auto* proto = static_cast<ddk::AnyProtocol*>(out);
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

zx_off_t Device::DdkGetSize() {
  LOG_ENTRY();

  zx_off_t reserved, size;
  if (mul_overflow(info_.block_size, info_.reserved_blocks, &reserved) ||
      sub_overflow(device_get_size(info_.block_device), reserved, &size)) {
    zxlogf(ERROR, "device_get_size returned less than what has been reserved");
    return 0;
  }

  return size;
}

// TODO(aarongreen): See ZX-1138.  Currently, there's no good way to trigger
// this on demand.
void Device::DdkUnbindNew(ddk::UnbindTxn txn) {
  LOG_ENTRY();
  bool was_active = active_.exchange(false);
  ZX_ASSERT(was_active);
  txn.Reply();
}

void Device::DdkRelease() {
  LOG_ENTRY();

  // One way or another we need to release the memory
  auto cleanup = fbl::MakeAutoCall([this]() {
    zxlogf(DEBUG, "zxcrypt device %p released", this);
    delete this;
  });

  // Stop workers; send a stop message to each, then join each (possibly in different order).
  StopWorkersIfDone();
  for (size_t i = 0; i < kNumWorkers; ++i) {
    workers_[i].Stop();
  }
}

////////////////////////////////////////////////////////////////
// ddk::BlockProtocol methods

void Device::BlockImplQuery(block_info_t* out_info, size_t* out_op_size) {
  LOG_ENTRY_ARGS("out_info=%p, out_op_size=%p", out_info, out_op_size);

  info_.block_protocol.Query(out_info, out_op_size);
  out_info->block_count -= info_.reserved_blocks;
  // Cap largest transaction to a quarter of the VMO buffer.
  out_info->max_transfer_size = std::min(Volume::kBufferSize / 4, out_info->max_transfer_size);
  *out_op_size = info_.op_size;
}

void Device::BlockImplQueue(block_op_t* block, block_impl_queue_callback completion_cb,
                            void* cookie) {
  LOG_ENTRY_ARGS("block=%p", block);

  // Check if the device is active.
  if (!active_.load()) {
    zxlogf(ERROR, "rejecting I/O request: device is not active");
    completion_cb(cookie, ZX_ERR_BAD_STATE, block);
    return;
  }
  num_ops_.fetch_add(1);

  // Initialize our extra space and save original values
  extra_op_t* extra = BlockToExtra(block, info_.op_size);
  zx_status_t rc = extra->Init(block, completion_cb, cookie, info_.reserved_blocks);
  if (rc != ZX_OK) {
    zxlogf(ERROR, "failed to initialize extra info: %s", zx_status_get_string(rc));
    BlockComplete(block, rc);
    return;
  }

  switch (block->command & BLOCK_OP_MASK) {
    case BLOCK_OP_WRITE:
      EnqueueWrite(block);
      break;
    case BLOCK_OP_READ:
    default:
      BlockForward(block, ZX_OK);
      break;
  }
}

////////////////////////////////////////////////////////////////
// ddk::PartitionProtocol methods

zx_status_t Device::BlockPartitionGetGuid(guidtype_t guidtype, guid_t* out_guid) {
  if (!info_.partition_protocol.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return info_.partition_protocol.GetGuid(guidtype, out_guid);
}

zx_status_t Device::BlockPartitionGetName(char* out_name, size_t capacity) {
  if (!info_.partition_protocol.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return info_.partition_protocol.GetName(out_name, capacity);
}

////////////////////////////////////////////////////////////////
// ddk::VolumeProtocol methods
zx_status_t Device::BlockVolumeExtend(const slice_extent_t* extent) {
  if (!info_.volume_protocol.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  slice_extent_t modified = *extent;
  modified.offset += info_.reserved_slices;
  return info_.volume_protocol.Extend(&modified);
}

zx_status_t Device::BlockVolumeShrink(const slice_extent_t* extent) {
  if (!info_.volume_protocol.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  slice_extent_t modified = *extent;
  modified.offset += info_.reserved_slices;
  return info_.volume_protocol.Shrink(&modified);
}

zx_status_t Device::BlockVolumeQuery(parent_volume_info_t* out_info) {
  if (!info_.volume_protocol.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t status = info_.volume_protocol.Query(out_info);
  if (status != ZX_OK) {
    return status;
  }

  out_info->virtual_slice_count -= info_.reserved_slices;
  out_info->physical_slice_count_total -= info_.reserved_slices;
  out_info->physical_slice_count_used -= info_.reserved_slices;

  return ZX_OK;
}

zx_status_t Device::BlockVolumeQuerySlices(const uint64_t* start_list, size_t start_count,
                                           slice_region_t* out_responses_list,
                                           size_t responses_count, size_t* out_responses_actual) {
  if (!info_.volume_protocol.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  ZX_DEBUG_ASSERT(start_count <= MAX_SLICE_QUERY_REQUESTS);

  uint64_t modified_list[start_count];
  memcpy(modified_list, start_list, start_count);
  for (size_t i = 0; i < start_count; i++) {
    modified_list[i] = start_list[i] + info_.reserved_slices;
  }
  return info_.volume_protocol.QuerySlices(modified_list, start_count, out_responses_list,
                                           responses_count, out_responses_actual);
}

zx_status_t Device::BlockVolumeDestroy() {
  if (!info_.volume_protocol.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return info_.volume_protocol.Destroy();
}

void Device::BlockForward(block_op_t* block, zx_status_t status) {
  LOG_ENTRY_ARGS("block=%p, status=%s", block, zx_status_get_string(status));

  if (!block) {
    zxlogf(TRACE, "early return; no block provided");
    return;
  }
  if (status != ZX_OK) {
    zxlogf(ERROR, "aborting request due to failure: %s", zx_status_get_string(status));
    BlockComplete(block, status);
    return;
  }
  // Check if the device is active (i.e. |DdkUnbind| has not been called).
  if (!active_.load()) {
    zxlogf(ERROR, "aborting request; device is not active");
    BlockComplete(block, ZX_ERR_BAD_STATE);
    return;
  }

  // Send the request to the parent device
  info_.block_protocol.Queue(block, BlockCallback, this);
}

void Device::BlockComplete(block_op_t* block, zx_status_t status) {
  LOG_ENTRY_ARGS("block=%p, status=%s", block, zx_status_get_string(status));
  zx_status_t rc;

  // If a portion of the write buffer was allocated, release it.
  extra_op_t* extra = BlockToExtra(block, info_.op_size);
  if (extra->data) {
    uint64_t off = (extra->data - info_.base) / info_.block_size;
    uint64_t len = block->rw.length;
    extra->data = nullptr;

    fbl::AutoLock lock(&mtx_);
    ZX_DEBUG_ASSERT(map_.Get(off, off + len));
    rc = map_.Clear(off, off + len);
    ZX_DEBUG_ASSERT(rc == ZX_OK);
  }

  // Complete the request.
  extra->completion_cb(extra->cookie, status, block);

  // If we previously stalled, try to re-queue the deferred requests; otherwise, avoid taking the
  // lock.
  if (stalled_.exchange(false)) {
    EnqueueWrite();
  }

  if (num_ops_.fetch_sub(1) == 1) {
    StopWorkersIfDone();
  }
}

////////////////////////////////////////////////////////////////
// Private methods

void Device::EnqueueWrite(block_op_t* block) {
  LOG_ENTRY_ARGS("block=%p", block);
  zx_status_t rc = ZX_OK;

  fbl::AutoLock lock(&mtx_);

  // Append the request to the write queue (if not null)
  extra_op_t* extra;
  if (block) {
    extra = BlockToExtra(block, info_.op_size);
    list_add_tail(&queue_, &extra->node);
  }
  if (stalled_.load()) {
    zxlogf(TRACE, "early return; no requests completed since last stall");
    return;
  }

  // Process as many pending write requests as we can right now.
  list_node_t pending;
  list_initialize(&pending);
  while (!list_is_empty(&queue_)) {
    extra = list_peek_head_type(&queue_, extra_op_t, node);
    block = ExtraToBlock(extra, info_.op_size);

    // Find an available offset in the write buffer
    uint64_t off;
    uint64_t len = block->rw.length;
    if ((rc = map_.Find(false, hint_, map_.size(), len, &off)) == ZX_ERR_NO_RESOURCES &&
        (rc = map_.Find(false, 0, map_.size(), len, &off)) == ZX_ERR_NO_RESOURCES) {
      zxlogf(DEBUG, "zxcrypt device %p stalled pending request completion", this);
      stalled_.store(true);
      break;
    }

    // We don't expect any other errors
    ZX_DEBUG_ASSERT(rc == ZX_OK);
    rc = map_.Set(off, off + len);
    ZX_DEBUG_ASSERT(rc == ZX_OK);

    // Save a hint as to where to start looking next time
    hint_ = (off + len) % map_.size();

    // Modify request to use write buffer
    extra->data = info_.base + (off * info_.block_size);
    block->rw.vmo = info_.vmo.get();
    block->rw.offset_vmo = (extra->data - info_.base) / info_.block_size;

    list_add_tail(&pending, list_remove_head(&queue_));
  }

  // Release the lock and send blocks that are ready to the workers
  lock.release();
  extra_op_t* tmp;
  list_for_every_entry_safe (&pending, extra, tmp, extra_op_t, node) {
    list_delete(&extra->node);
    block = ExtraToBlock(extra, info_.op_size);
    SendToWorker(block);
  }
}

void Device::SendToWorker(block_op_t* block) {
  LOG_ENTRY_ARGS("block=%p", block);
  zx_status_t rc;

  zx_port_packet_t packet;
  Worker::MakeRequest(&packet, Worker::kBlockRequest, block);
  if ((rc = port_.queue(&packet)) != ZX_OK) {
    zxlogf(ERROR, "zx::port::queue failed: %s", zx_status_get_string(rc));
    BlockComplete(block, rc);
    return;
  }
}

void Device::BlockCallback(void* cookie, zx_status_t status, block_op_t* block) {
  LOG_ENTRY_ARGS("block=%p, status=%s", block, zx_status_get_string(status));

  // Restore data that may have changed
  Device* device = static_cast<Device*>(cookie);
  extra_op_t* extra = BlockToExtra(block, device->op_size());
  block->rw.vmo = extra->vmo;
  block->rw.length = extra->length;
  block->rw.offset_dev = extra->offset_dev;
  block->rw.offset_vmo = extra->offset_vmo;

  if (status != ZX_OK) {
    zxlogf(DEBUG, "parent device returned %s", zx_status_get_string(status));
    device->BlockComplete(block, status);
    return;
  }
  switch (block->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
      device->SendToWorker(block);
      break;
    case BLOCK_OP_WRITE:
    default:
      device->BlockComplete(block, ZX_OK);
      break;
  }
}

void Device::StopWorkersIfDone() {
  // Multiple threads may pass this check, but that's harmless.
  if (!active_.load() && num_ops_.load() == 0) {
    zx_port_packet_t packet;
    Worker::MakeRequest(&packet, Worker::kStopRequest);
    for (size_t i = 0; i < kNumWorkers; ++i) {
      port_.queue(&packet);
    }
  }
}

}  // namespace zxcrypt
