// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_balloon.h"

#include <fbl/auto_lock.h>
#include <zircon/syscalls.h>

namespace machina {

// Structure passed to the inflate/deflate queue handler.
typedef struct queue_ctx {
  // The VMO to invoke |op| on.
  zx_handle_t vmo;
  // Operation to perform on the queue (inflate or deflate).
  uint32_t op;
} queue_ctx_t;

/* Handle balloon inflate/deflate requests.
 *
 * From VIRTIO 1.0 Section 5.5.6:
 *
 * To supply memory to the balloon (aka. inflate):
 *  (a) The driver constructs an array of addresses of unused memory pages.
 *      These addresses are divided by 4096 and the descriptor describing the
 *      resulting 32-bit array is added to the inflateq.
 *
 * To remove memory from the balloon (aka. deflate):
 *  (a) The driver constructs an array of addresses of memory pages it has
 *      previously given to the balloon, as described above. This descriptor is
 *      added to the deflateq.
 *  (b) If the VIRTIO_BALLOON_F_MUST_TELL_HOST feature is negotiated, the guest
 *      informs the device of pages before it uses them.
 *  (c) Otherwise, the guest is allowed to re-use pages previously given to the
 *      balloon before the device has acknowledged their withdrawal.
 */
static zx_status_t queue_range_op(void* addr, uint32_t len, uint16_t flags,
                                  uint32_t* used, void* ctx) {
  queue_ctx_t* balloon_ctx = static_cast<queue_ctx_t*>(ctx);
  uint32_t* pfns = static_cast<uint32_t*>(addr);
  uint32_t pfn_count = len / 4;

  // If the driver writes contiguous PFNs to the array we'll batch them up
  // when invoking the inflate/deflate operation.
  uint64_t region_base = 0;
  uint64_t region_length = 0;
  for (uint32_t i = 0; i < pfn_count; ++i) {
    // If we have a contiguous page, increment the length & continue.
    if (region_length > 0 && (region_base + region_length) == pfns[i]) {
      region_length++;
      continue;
    }

    // If we have an existing region; invoke the inflate/deflate op.
    if (region_length > 0) {
      zx_status_t status =
          zx_vmo_op_range(balloon_ctx->vmo, balloon_ctx->op,
                          region_base * VirtioBalloon::kPageSize,
                          region_length * VirtioBalloon::kPageSize, nullptr, 0);
      if (status != ZX_OK) {
        return status;
      }
    }

    // Create a new region.
    region_base = pfns[i];
    region_length = 1;
  }

  // Handle the last region.
  if (region_length > 0) {
    zx_status_t status =
        zx_vmo_op_range(balloon_ctx->vmo, balloon_ctx->op,
                        region_base * VirtioBalloon::kPageSize,
                        region_length * VirtioBalloon::kPageSize, nullptr, 0);
    if (status != ZX_OK) {
      return status;
    }
  }

  return ZX_OK;
}

zx_status_t VirtioBalloon::HandleDescriptor(uint16_t queue_sel) {
  queue_ctx_t ctx;
  switch (queue_sel) {
    case VIRTIO_BALLOON_Q_STATSQ:
      return ZX_OK;
    case VIRTIO_BALLOON_Q_INFLATEQ:
      ctx.op = ZX_VMO_OP_DECOMMIT;
      break;
    case VIRTIO_BALLOON_Q_DEFLATEQ:
      if (deflate_on_demand_) {
        return ZX_OK;
      }
      ctx.op = ZX_VMO_OP_COMMIT;
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  ctx.vmo = phys_mem().vmo().get();
  return queue(queue_sel)->HandleDescriptor(&queue_range_op, &ctx);
}

zx_status_t VirtioBalloon::HandleQueueNotify(uint16_t queue_sel) {
  zx_status_t status;
  do {
    status = HandleDescriptor(queue_sel);
  } while (status == ZX_ERR_NEXT);
  return status;
}

VirtioBalloon::VirtioBalloon(const PhysMem& phys_mem)
    : VirtioDeviceBase(phys_mem) {
  add_device_features(VIRTIO_BALLOON_F_STATS_VQ |
                      VIRTIO_BALLOON_F_DEFLATE_ON_OOM);
}

void VirtioBalloon::WaitForStatsBuffer(VirtioQueue* stats_queue) {
  if (!stats_.has_buffer) {
    stats_queue->Wait(&stats_.desc_index);
    stats_.has_buffer = true;
  }
}

zx_status_t VirtioBalloon::RequestStats(StatsHandler handler) {
  VirtioQueue* stats_queue = queue(VIRTIO_BALLOON_Q_STATSQ);

  // stats.mutex needs to be held during the entire time the guest is
  // processing the buffer since we need to make sure no other threads
  // can grab the returned stats buffer before we process it.
  fbl::AutoLock lock(&stats_.mutex);

  // We need an initial buffer we can return to return to the device to
  // request stats from the device. This should be immediately available in
  // the common case but we can race the driver for the initial buffer.
  WaitForStatsBuffer(stats_queue);

  // We have a buffer. We need to return it to the driver. It'll populate
  // a new buffer with stats and then send it back to us.
  stats_.has_buffer = false;
  zx_status_t status = stats_queue->Return(stats_.desc_index, 0);
  if (status != ZX_OK) {
    return status;
  }
  WaitForStatsBuffer(stats_queue);

  virtio_desc_t desc;
  status = stats_queue->ReadDesc(stats_.desc_index, &desc);
  if (status != ZX_OK) {
    return status;
  }

  if ((desc.len % sizeof(virtio_balloon_stat_t)) != 0) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  // Invoke the handler on the stats.
  auto stats = static_cast<const virtio_balloon_stat_t*>(desc.addr);
  size_t stats_count = desc.len / sizeof(virtio_balloon_stat_t);
  handler(stats, stats_count);

  // Note we deliberately do not return the buffer here. This will be done to
  // initiate the next stats request.
  return ZX_OK;
}

zx_status_t VirtioBalloon::UpdateNumPages(uint32_t num_pages) {
  {
    fbl::AutoLock lock(&config_mutex_);
    config_.num_pages = num_pages;
  }

  // Send a config change interrupt to the guest.
  add_isr_flags(VirtioDevice::VIRTIO_ISR_DEVICE);
  return NotifyGuest();
}

uint32_t VirtioBalloon::num_pages() {
  fbl::AutoLock lock(&config_mutex_);
  return config_.num_pages;
}

}  // namespace machina
