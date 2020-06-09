// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scsi.h"

#include <inttypes.h>
#include <lib/scsi/scsilib.h>
#include <lib/scsi/scsilib_controller.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <zircon/compiler.h>

#include <utility>

#include <ddk/debug.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <pretty/hexdump.h>
#include <virtio/scsi.h>

#include "src/devices/bus/lib/virtio/trace.h"

#define LOCAL_TRACE 0

namespace virtio {

// Fill in req->lun with a single-level LUN structure representing target:lun.
void ScsiDevice::FillLUNStructure(struct virtio_scsi_req_cmd* req, uint8_t target, uint16_t lun) {
  req->lun[0] = 1;
  req->lun[1] = target;
  req->lun[2] = 0x40 | static_cast<uint8_t>(lun >> 8);
  req->lun[3] = static_cast<uint8_t>(lun) & 0xff;
}

ScsiDevice::scsi_io_slot* ScsiDevice::GetIO() {
  // For testing purposes, this condition can be triggered
  // by lowering MAX_IOS (to say 2). And running biotime
  // (with default IO concurrency).
  while (active_ios_ == MAX_IOS) {
    ioslot_cv_.Wait(&lock_);
  }
  active_ios_++;
  for (int i = 0; i < MAX_IOS; i++) {
    if (scsi_io_slot_table_[i].avail) {
      scsi_io_slot_table_[i].avail = false;
      return &scsi_io_slot_table_[i];
    }
  }
  ZX_DEBUG_ASSERT(false);  // Unexpected.
  return NULL;
}

void ScsiDevice::FreeIO(scsi_io_slot* io_slot) {
  io_slot->avail = true;
  active_ios_--;
  ioslot_cv_.Signal();
}

void ScsiDevice::IrqRingUpdate() {
  // Parse our descriptor chain and add back to the free queue.
  auto free_chain = [this](vring_used_elem* elem) TA_NO_THREAD_SAFETY_ANALYSIS {
    auto index = static_cast<uint16_t>(elem->id);
    vring_desc const* tail_desc;

    // Reclaim the entire descriptor chain.
    for (;;) {
      vring_desc const* desc = request_queue_.DescFromIndex(index);
      const bool has_next = desc->flags & VRING_DESC_F_NEXT;
      const auto next = desc->next;

      this->request_queue_.FreeDesc(index);
      if (!has_next) {
        tail_desc = desc;
        break;
      }
      index = next;
    }
    desc_cv_.Broadcast();
    // Search for the IO that just completed, using tail_desc.
    for (int i = 0; i < MAX_IOS; i++) {
      scsi_io_slot* io_slot = &scsi_io_slot_table_[i];

      if (io_slot->avail)
        continue;
      if (io_slot->tail_desc == tail_desc) {
        // Capture response before freeing iobuffer.
        zx_status_t status;
        if (io_slot->response->response || io_slot->response->status) {
          status = ZX_ERR_INTERNAL;
        } else {
          status = ZX_OK;
        }
        // If Read, copy data from iobuffer to the iovec.
        if (status == ZX_OK && io_slot->data_in.iov_len) {
          memcpy(io_slot->data_in.iov_base, io_slot->data_in_region, io_slot->data_in.iov_len);
        }
        void* cookie = io_slot->cookie;
        auto (*callback)(void* cookie, zx_status_t status) = io_slot->callback;
        FreeIO(io_slot);
        lock_.Release();
        callback(cookie, status);
        lock_.Acquire();
        return;
      }
    }
    ZX_DEBUG_ASSERT(false);  // Unexpected.
  };

  // Tell the ring to find free chains and hand it back to our lambda.
  fbl::AutoLock lock(&lock_);
  request_queue_.IrqRingUpdate(free_chain);
}

zx_status_t ScsiDevice::ExecuteCommandSync(uint8_t target, uint16_t lun, struct iovec cdb,
                                           struct iovec data_out, struct iovec data_in) {
  struct scsi_sync_callback_state {
    sync_completion_t completion;
    zx_status_t status;
  };
  scsi_sync_callback_state cookie;
  sync_completion_reset(&cookie.completion);
  auto callback = [](void* cookie, zx_status_t status) {
    auto* state = reinterpret_cast<scsi_sync_callback_state*>(cookie);
    state->status = status;
    sync_completion_signal(&state->completion);
  };
  ExecuteCommandAsync(target, lun, cdb, data_out, data_in, callback, &cookie);
  sync_completion_wait(&cookie.completion, ZX_TIME_INFINITE);
  return cookie.status;
}

zx_status_t ScsiDevice::ExecuteCommandAsync(uint8_t target, uint16_t lun, struct iovec cdb,
                                            struct iovec data_out, struct iovec data_in,
                                            void (*cb)(void*, zx_status_t), void* cookie) {
  // We do all of the error checking up front, so we don't need to fail the IO
  // after acquiring the IO slot and the descriptors.
  // If data_in fits within request_buffers_, all the regions of this request will fit.
  if ((sizeof(struct virtio_scsi_req_cmd) + data_out.iov_len + sizeof(struct virtio_scsi_resp_cmd) +
       data_in.iov_len) > request_buffers_size_) {
    return ZX_ERR_NO_MEMORY;
  }

  uint16_t descriptor_chain_length = 2;
  if (data_out.iov_len) {
    descriptor_chain_length++;
  }
  if (data_in.iov_len) {
    descriptor_chain_length++;
  }

  lock_.Acquire();
  // Get both the IO slot and the descriptors needed up front.
  auto io_slot = GetIO();
  uint16_t id = 0;
  auto request_desc = request_queue_.AllocDescChain(/*count=*/descriptor_chain_length, &id);
  // For testing purposes, this condition can be triggered by failing
  // AllocDescChain every N attempts. But we would have to Signal the cv
  // somewhere. A good place to do that is at the bottom of WorkerThread,
  // after the luns are probed, in a loop. If we do the signaling there,
  // we'd need to ensure error injection doesn't start until after luns are
  // probed.
  while (request_desc == nullptr) {
    // Drop the request buf, before blocking, waiting for descs to free up.
    FreeIO(io_slot);
    desc_cv_.Wait(&lock_);
    io_slot = GetIO();
    request_desc = request_queue_.AllocDescChain(/*count=*/descriptor_chain_length, &id);
  }

  auto* request_buffers = &io_slot->request_buffer;
  // virtio-scsi requests have a 'request' region, an optional data-out region, a 'response'
  // region, and an optional data-in region. Allocate and fill them and then execute the request.
  const auto request_offset = 0ull;
  const auto data_out_offset = request_offset + sizeof(struct virtio_scsi_req_cmd);
  const auto response_offset = data_out_offset + data_out.iov_len;
  const auto data_in_offset = response_offset + sizeof(struct virtio_scsi_resp_cmd);

  auto* const request_buffers_addr = reinterpret_cast<uint8_t*>(io_buffer_virt(request_buffers));
  auto* const request =
      reinterpret_cast<struct virtio_scsi_req_cmd*>(request_buffers_addr + request_offset);
  auto* const data_out_region = reinterpret_cast<uint8_t*>(request_buffers_addr + data_out_offset);
  auto* const response =
      reinterpret_cast<struct virtio_scsi_resp_cmd*>(request_buffers_addr + response_offset);
  auto* const data_in_region = reinterpret_cast<uint8_t*>(request_buffers_addr + data_in_offset);

  memset(request, 0, sizeof(*request));
  memset(response, 0, sizeof(*response));
  memcpy(&request->cdb, cdb.iov_base, cdb.iov_len);
  FillLUNStructure(request, /*target=*/target, /*lun=*/lun);
  request->id = scsi_transport_tag_++;

  vring_desc* tail_desc;
  request_desc->addr = io_buffer_phys(request_buffers) + request_offset;
  request_desc->len = sizeof(*request);
  request_desc->flags = VRING_DESC_F_NEXT;
  auto next_id = request_desc->next;

  if (data_out.iov_len) {
    memcpy(data_out_region, data_out.iov_base, data_out.iov_len);
    auto* data_out_desc = request_queue_.DescFromIndex(next_id);
    data_out_desc->addr = io_buffer_phys(request_buffers) + data_out_offset;
    data_out_desc->len = static_cast<uint32_t>(data_out.iov_len);
    data_out_desc->flags = VRING_DESC_F_NEXT;
    next_id = data_out_desc->next;
  }

  auto* response_desc = request_queue_.DescFromIndex(next_id);
  response_desc->addr = io_buffer_phys(request_buffers) + response_offset;
  response_desc->len = sizeof(*response);
  response_desc->flags = VRING_DESC_F_WRITE;

  if (data_in.iov_len) {
    response_desc->flags |= VRING_DESC_F_NEXT;
    auto* data_in_desc = request_queue_.DescFromIndex(response_desc->next);
    data_in_desc->addr = io_buffer_phys(request_buffers) + data_in_offset;
    data_in_desc->len = static_cast<uint32_t>(data_in.iov_len);
    data_in_desc->flags = VRING_DESC_F_WRITE;
    tail_desc = data_in_desc;
  } else {
    tail_desc = response_desc;
  }

  io_slot->tail_desc = tail_desc;
  io_slot->data_in = data_in;
  io_slot->data_in_region = data_in_region;
  io_slot->callback = cb;
  io_slot->cookie = cookie;
  io_slot->request_buffers = request_buffers;
  io_slot->response = response;

  request_queue_.SubmitChain(id);
  request_queue_.Kick();

  lock_.Release();
  return ZX_OK;
}

constexpr uint32_t SCSI_SECTOR_SIZE = 512;
constexpr uint32_t SCSI_MAX_XFER_SIZE = 1024;  // 512K clamp

// Read Block Limits VPD Page (0xB0), if supported and return the max xfer size
// (in blocks) supported by the target.
zx_status_t ScsiDevice::TargetMaxXferSize(uint8_t target, uint16_t lun,
                                          uint32_t& xfer_size_sectors) {
  scsi::InquiryCDB inquiry_cdb = {};
  scsi::VPDPageList vpd_pagelist = {};
  inquiry_cdb.opcode = scsi::Opcode::INQUIRY;
  // Query for all supported VPD pages.
  inquiry_cdb.reserved_and_evpd = 0x1;
  inquiry_cdb.page_code = 0x00;
  inquiry_cdb.allocation_length = ntohs(sizeof(vpd_pagelist));
  auto status = ExecuteCommandSync(/*target=*/target, /*lun=*/lun,
                                   /*cdb=*/{&inquiry_cdb, sizeof(inquiry_cdb)},
                                   /*data_out=*/{nullptr, 0},
                                   /*data_in=*/{&vpd_pagelist, sizeof(vpd_pagelist)});
  if (status != ZX_OK)
    return status;
  uint8_t i;
  for (i = 0; i < vpd_pagelist.page_length; i++) {
    if (vpd_pagelist.pages[i] == 0xB0)
      break;
  }
  if (i == vpd_pagelist.page_length)
    return ZX_ERR_NOT_SUPPORTED;
  // The Block Limits VPD page is supported, fetch it.
  scsi::VPDBlockLimits block_limits = {};
  inquiry_cdb.page_code = 0xB0;
  inquiry_cdb.allocation_length = ntohs(sizeof(block_limits));
  status = ExecuteCommandSync(/*target=*/target, /*lun=*/lun,
                              /*cdb=*/{&inquiry_cdb, sizeof(inquiry_cdb)},
                              /*data_out=*/{nullptr, 0},
                              /*data_in=*/{&block_limits, sizeof(block_limits)});
  if (status != ZX_OK)
    return status;
  xfer_size_sectors = block_limits.max_xfer_length_blocks;
  return ZX_OK;
}

zx_status_t ScsiDevice::WorkerThread() {
  uint16_t max_target;
  uint32_t max_lun;
  uint32_t max_sectors;  // controller's max sectors.
  {
    fbl::AutoLock lock(&lock_);
    // virtio-scsi has a 16-bit max_target field, but the encoding we use limits us to one byte
    // target identifiers.
    max_target = fbl::min(config_.max_target, static_cast<uint16_t>(UINT8_MAX - 1));
    max_lun = config_.max_lun;
    max_sectors = config_.max_sectors;
  }

  // Execute TEST UNIT READY on every possible target to find potential disks.
  // TODO(ZX-2314): For SCSI-3 targets, we could optimize this by using REPORT LUNS.
  //
  // virtio-scsi nominally supports multiple channels, but the device support is not
  // complete. The device encoding for targets in commands does not allow encoding the
  // channel number, so we do not attempt to scan beyond channel 0 here.
  //
  // QEMU and GCE disagree on the definition of the max_target and max_lun config fields;
  // QEMU's max_target/max_lun refer to the last valid whereas GCE's refers to the first
  // invalid target/lun. Use <= to handle both.
  //
  // TODO(ZX-2314): Move probe sequence to ScsiLib -- have it call down into LLDs to execute
  // commands.
  for (uint8_t target = 0u; target <= max_target; target++) {
    const uint32_t luns_on_this_target = CountLuns(this, target);
    if (luns_on_this_target == 0) {
      continue;
    }

    uint16_t luns_found = 0;
    uint32_t max_xfer_size_sectors = 0;
    for (uint16_t lun = 0u; lun <= max_lun; lun++) {
      scsi::TestUnitReadyCDB cdb = {};
      cdb.opcode = scsi::Opcode::TEST_UNIT_READY;

      auto status = ExecuteCommandSync(
          /*target=*/target,
          /*lun=*/lun, {&cdb, sizeof(cdb)}, {}, {});
      if ((status == ZX_OK) && (max_xfer_size_sectors == 0)) {
        // If we haven't queried the VPD pages for the target's xfer size
        // yet, do it now. We only query this once per target.
        status = TargetMaxXferSize(target, lun, max_xfer_size_sectors);
        if (status == ZX_OK) {
          // smaller of controller and target max_xfer_sizes
          max_xfer_size_sectors = fbl::min(max_xfer_size_sectors, max_sectors);
          // and the 512K clamp
          max_xfer_size_sectors = fbl::min(max_xfer_size_sectors, SCSI_MAX_XFER_SIZE);
        } else {
          max_xfer_size_sectors = fbl::min(max_sectors, SCSI_MAX_XFER_SIZE);
        }
        zxlogf(INFO, "Virtio SCSI %u:%u Max Xfer Size %ukb", target, lun,
               max_xfer_size_sectors * 2);
        scsi::Disk::Create(this, device_, /*target=*/target, /*lun=*/lun, max_xfer_size_sectors);
        luns_found++;
      }
      // If we've found all the LUNs present on this target, move on.
      // Subtle detail - LUN 0 may respond to TEST UNIT READY even if it is not a valid LUN
      // and there is a valid LUN elsewhere on the target. Test for one more LUN than we
      // expect to work around that.
      if (luns_found > luns_on_this_target) {
        break;
      }
    }
  }
  return ZX_OK;
}

zx_status_t ScsiDevice::Init() {
  LTRACE_ENTRY;

  virtio::Device::DeviceReset();
  virtio::Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, num_queues),
                                             &config_.num_queues);
  virtio::Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, seg_max),
                                             &config_.seg_max);
  virtio::Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, max_sectors),
                                             &config_.max_sectors);
  virtio::Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, cmd_per_lun),
                                             &config_.cmd_per_lun);
  virtio::Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, event_info_size),
                                             &config_.event_info_size);
  virtio::Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, sense_size),
                                             &config_.sense_size);
  virtio::Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, cdb_size),
                                             &config_.cdb_size);
  virtio::Device::ReadDeviceConfig<uint16_t>(offsetof(virtio_scsi_config, max_channel),
                                             &config_.max_channel);
  virtio::Device::ReadDeviceConfig<uint16_t>(offsetof(virtio_scsi_config, max_target),
                                             &config_.max_target);
  virtio::Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, max_lun),
                                             &config_.max_lun);

  // Validate config.
  {
    fbl::AutoLock lock(&lock_);
    if (config_.max_channel > 1) {
      zxlogf(WARN, "config_.max_channel %d not expected.", config_.max_channel);
    }
  }

  virtio::Device::DriverStatusAck();

  if (!bti().is_valid()) {
    zxlogf(ERROR, "invalid bti handle");
    return ZX_ERR_BAD_HANDLE;
  }
  {
    fbl::AutoLock lock(&lock_);
    auto err = control_ring_.Init(/*index=*/Queue::CONTROL);
    if (err) {
      zxlogf(ERROR, "failed to allocate control queue");
      return err;
    }

    err = request_queue_.Init(/*index=*/Queue::REQUEST);
    if (err) {
      zxlogf(ERROR, "failed to allocate request queue");
      return err;
    }
    request_buffers_size_ =
        (SCSI_SECTOR_SIZE * fbl::min(config_.max_sectors, SCSI_MAX_XFER_SIZE)) +
        (sizeof(struct virtio_scsi_req_cmd) + sizeof(struct virtio_scsi_resp_cmd));
    for (int i = 0; i < MAX_IOS; i++) {
      auto status = io_buffer_init(&scsi_io_slot_table_[i].request_buffer, bti().get(),
                                   /*size=*/request_buffers_size_, IO_BUFFER_RW | IO_BUFFER_CONTIG);
      if (status) {
        zxlogf(ERROR, "failed to allocate queue working memory");
        return status;
      }
      scsi_io_slot_table_[i].avail = true;
    }
    active_ios_ = 0;
    scsi_transport_tag_ = 0;
  }
  virtio::Device::StartIrqThread();
  virtio::Device::DriverStatusOk();

  // Synchronize against Unbind()/Release() before the worker thread is running.
  fbl::AutoLock lock(&lock_);
  auto status = DdkAdd("virtio-scsi");
  device_ = zxdev();
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to run DdkAdd");
    device_ = nullptr;
    return status;
  }

  auto td = [](void* ctx) {
    ScsiDevice* const device = static_cast<ScsiDevice*>(ctx);
    return device->WorkerThread();
  };
  int ret = thrd_create_with_name(&worker_thread_, td, this, "virtio-scsi-worker");
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  return status;
}

void ScsiDevice::DdkUnbindNew(ddk::UnbindTxn txn) { virtio::Device::Unbind(std::move(txn)); }

void ScsiDevice::DdkRelease() {
  {
    fbl::AutoLock lock(&lock_);
    worker_thread_should_exit_ = true;
    for (int i = 0; i < MAX_IOS; i++) {
      io_buffer_release(&scsi_io_slot_table_[i].request_buffer);
    }
  }
  thrd_join(worker_thread_, nullptr);
  virtio::Device::Release();
}

}  // namespace virtio
