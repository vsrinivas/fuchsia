// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_block.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <block-client/client.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <trace-engine/types.h>
#include <trace/event.h>
#include <virtio/virtio_ids.h>
#include <virtio/virtio_ring.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>

#include "lib/fxl/logging.h"

namespace machina {

VirtioBlock::VirtioBlock(const PhysMem& phys_mem) : VirtioDeviceBase(phys_mem) {
  config_.blk_size = kSectorSize;
  // Virtio 1.0: 5.2.5.2: Devices SHOULD always offer VIRTIO_BLK_F_FLUSH
  add_device_features(VIRTIO_BLK_F_FLUSH
                      // Required by zircon guests.
                      | VIRTIO_BLK_F_BLK_SIZE);
}

zx_status_t VirtioBlock::SetDispatcher(
    fbl::unique_ptr<BlockDispatcher> dispatcher) {
  if (dispatcher_ != nullptr) {
    FXL_LOG(ERROR) << "Block device has already been initialized";
    return ZX_ERR_BAD_STATE;
  }

  dispatcher_ = std::move(dispatcher);
  {
    fbl::AutoLock lock(&config_mutex_);
    config_.capacity = dispatcher_->size() / kSectorSize;
  }
  if (dispatcher_->read_only()) {
    add_device_features(VIRTIO_BLK_F_RO);
  }
  return ZX_OK;
}

zx_status_t VirtioBlock::Start() {
  return queue(0)->Poll(
      fit::bind_member(this, &VirtioBlock::HandleBlockRequest), "virtio-block");
}

zx_status_t VirtioBlock::HandleBlockRequest(VirtioQueue* queue, uint16_t head,
                                            uint32_t* used) {
  // Attempt to correlate the processing of descriptors with a previous kick.
  // As noted in virtio_device.cc this should be considered best-effort only.
  const trace_async_id_t unset_id = 0;
  const trace_async_id_t flow_id = trace_flow_id(0)->exchange(unset_id);
  TRACE_DURATION("machina", "virtio_block_request", "flow_id", flow_id);
  if (flow_id != unset_id) {
    TRACE_FLOW_END("machina", "io_queue_signal", flow_id);
  }

  uint8_t block_status = VIRTIO_BLK_S_OK;
  uint8_t* block_status_ptr = nullptr;
  const virtio_blk_req_t* req = nullptr;
  off_t offset = 0;
  virtio_desc_t desc;

  zx_status_t status = queue->ReadDesc(head, &desc);
  if (status != ZX_OK) {
    desc.addr = nullptr;
    desc.len = 0;
    desc.has_next = false;
  }

  if (desc.len == sizeof(virtio_blk_req_t)) {
    req = static_cast<const virtio_blk_req_t*>(desc.addr);
  } else {
    block_status = VIRTIO_BLK_S_IOERR;
  }

  // VIRTIO 1.0 Section 5.2.6.2: A device MUST set the status byte to
  // VIRTIO_BLK_S_IOERR for a write request if the VIRTIO_BLK_F_RO feature
  // if offered, and MUST NOT write any data.
  if (req != nullptr && req->type == VIRTIO_BLK_T_OUT && is_read_only()) {
    block_status = VIRTIO_BLK_S_IOERR;
  }

  // VIRTIO Version 1.0: A driver MUST set sector to 0 for a
  // VIRTIO_BLK_T_FLUSH request. A driver SHOULD NOT include any data in a
  // VIRTIO_BLK_T_FLUSH request.
  if (req != nullptr && req->type == VIRTIO_BLK_T_FLUSH && req->sector != 0) {
    block_status = VIRTIO_BLK_S_IOERR;
  }

  // VIRTIO 1.0 Section 5.2.5.2: If the VIRTIO_BLK_F_BLK_SIZE feature is
  // negotiated, blk_size can be read to determine the optimal sector size
  // for the driver to use. This does not affect the units used in the
  // protocol (always 512 bytes), but awareness of the correct value can
  // affect performance.
  if (req != nullptr) {
    offset = req->sector * kSectorSize;
  }

  while (desc.has_next) {
    status = queue->ReadDesc(desc.next, &desc);
    if (status != ZX_OK) {
      block_status =
          block_status != VIRTIO_BLK_S_OK ? block_status : VIRTIO_BLK_S_IOERR;
      break;
    }

    // Requests should end with a single 1b status byte.
    if (desc.len == 1 && desc.writable && !desc.has_next) {
      block_status_ptr = static_cast<uint8_t*>(desc.addr);
      break;
    }

    // Skip doing any file ops if we've already encountered an error, but
    // keep traversing the descriptor chain looking for the status tailer.
    if (block_status != VIRTIO_BLK_S_OK) {
      continue;
    }

    zx_status_t status;
    switch (req->type) {
      case VIRTIO_BLK_T_IN:
        if (desc.len % kSectorSize != 0) {
          block_status = VIRTIO_BLK_S_IOERR;
          continue;
        }
        status = dispatcher_->Read(offset, desc.addr, desc.len);
        *used += desc.len;
        offset += desc.len;
        break;
      case VIRTIO_BLK_T_OUT: {
        if (desc.len % kSectorSize != 0) {
          block_status = VIRTIO_BLK_S_IOERR;
          continue;
        }
        status = dispatcher_->Write(offset, desc.addr, desc.len);
        offset += desc.len;
        break;
      }
      case VIRTIO_BLK_T_FLUSH:
        status = dispatcher_->Flush();
        break;
      default:
        block_status = VIRTIO_BLK_S_UNSUPP;
        break;
    }

    // Report any failures queuing the IO request.
    if (block_status == VIRTIO_BLK_S_OK && status != ZX_OK) {
      block_status = VIRTIO_BLK_S_IOERR;
    }
  }

  // Wait for operations to become consistent.
  status = dispatcher_->Submit();
  if (block_status == VIRTIO_BLK_S_OK && status != ZX_OK) {
    block_status = VIRTIO_BLK_S_IOERR;
  }

  // Set the output status if we found the byte in the descriptor chain.
  if (block_status_ptr != nullptr) {
    *block_status_ptr = block_status;
    ++*used;
  }
  return ZX_OK;
}

}  // namespace machina
