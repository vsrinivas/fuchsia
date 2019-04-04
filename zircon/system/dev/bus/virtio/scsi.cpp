// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scsi.h"

#include <ddk/debug.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <inttypes.h>
#include <pretty/hexdump.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <virtio/scsi.h>
#include <zircon/compiler.h>
#include <netinet/in.h>

#include <utility>

#include "scsilib.h"
#include "scsilib_controller.h"
#include "trace.h"

#define LOCAL_TRACE 0

namespace virtio {

// Fill in req->lun with a single-level LUN structure representing target:lun.
void ScsiDevice::FillLUNStructure(struct virtio_scsi_req_cmd* req, uint8_t target, uint16_t lun) {
    req->lun[0] = 1;
    req->lun[1] = target;
    req->lun[2] = 0x40 | static_cast<uint8_t>(lun >> 8);
    req->lun[3] = static_cast<uint8_t>(lun) & 0xff;
}

zx_status_t ScsiDevice::ExecuteCommandSync(uint8_t target, uint16_t lun,
                                           struct iovec cdb,
                                           struct iovec data_out,
                                           struct iovec data_in) {
    fbl::AutoLock lock(&lock_);
    // virtio-scsi requests have a 'request' region, an optional data-out region, a 'response'
    // region, and an optional data-in region. Allocate and fill them and then execute the request.
    const auto request_offset = 0ull;
    const auto data_out_offset = request_offset + sizeof(struct virtio_scsi_req_cmd);
    const auto response_offset = data_out_offset + data_out.iov_len;
    const auto data_in_offset = response_offset + sizeof(struct virtio_scsi_resp_cmd);
    // If data_in fits within request_buffers_, all the regions of this request will fit.
    if (data_in_offset + data_in.iov_len > request_buffers_.size) {
        return ZX_ERR_NO_MEMORY;
    }

    uint8_t* const request_buffers_addr =
        reinterpret_cast<uint8_t*>(io_buffer_virt(&request_buffers_));
    auto* const request =
        reinterpret_cast<struct virtio_scsi_req_cmd*>(request_buffers_addr + request_offset);
    auto* const data_out_region =
        reinterpret_cast<uint8_t*>(request_buffers_addr + data_out_offset);
    auto* const response =
        reinterpret_cast<struct virtio_scsi_resp_cmd*>(request_buffers_addr + response_offset);
    auto* const data_in_region =
        reinterpret_cast<uint8_t*>(request_buffers_addr + data_in_offset);

    memset(request, 0, sizeof(*request));
    memset(response, 0, sizeof(*response));
    memcpy(&request->cdb, cdb.iov_base, cdb.iov_len);
    FillLUNStructure(request, /*target=*/target, /*lun=*/lun);

    uint16_t descriptor_chain_length = 2;
    if (data_out.iov_len) {
        descriptor_chain_length++;
    }
    if (data_in.iov_len) {
        descriptor_chain_length++;
    }

    uint16_t id = 0;
    uint16_t next_id;
    auto request_desc = request_queue_.AllocDescChain(/*count=*/descriptor_chain_length, &id);
    request_desc->addr = io_buffer_phys(&request_buffers_) + request_offset;
    request_desc->len = sizeof(*request);
    request_desc->flags = VRING_DESC_F_NEXT;
    next_id = request_desc->next;

    if (data_out.iov_len) {
        memcpy(data_out_region, data_out.iov_base, data_out.iov_len);
        auto data_out_desc = request_queue_.DescFromIndex(next_id);
        data_out_desc->addr = io_buffer_phys(&request_buffers_) + data_out_offset;
        data_out_desc->len = static_cast<uint32_t>(data_out.iov_len);
        data_out_desc->flags = VRING_DESC_F_NEXT;
        next_id = data_out_desc->next;
    }

    auto response_desc = request_queue_.DescFromIndex(next_id);
    response_desc->addr = io_buffer_phys(&request_buffers_) + response_offset;
    response_desc->len = sizeof(*response);
    response_desc->flags = VRING_DESC_F_WRITE;

    if (data_in.iov_len) {
        response_desc->flags |= VRING_DESC_F_NEXT;
        auto data_in_desc = request_queue_.DescFromIndex(response_desc->next);
        data_in_desc->addr = io_buffer_phys(&request_buffers_) + data_in_offset;
        data_in_desc->len = static_cast<uint32_t>(data_in.iov_len);
        data_in_desc->flags = VRING_DESC_F_WRITE;
    }

    request_queue_.SubmitChain(id);
    request_queue_.Kick();

    // Wait for request to complete.
    sync_completion_t sync;
    for (;;) {
        // annotalysis is unable to determine that ScsiDevice::lock_ is held when the IrqRingUpdate
        // lambda is invoked.
        request_queue_.IrqRingUpdate(
            [this, &sync](vring_used_elem* elem) TA_NO_THREAD_SAFETY_ANALYSIS {
            auto index = static_cast<uint16_t>(elem->id);

            // Synchronously reclaim the entire descriptor chain.
            for (;;) {
                vring_desc const* desc = request_queue_.DescFromIndex(index);
                const bool has_next = desc->flags & VRING_DESC_F_NEXT;
                const uint16_t next = desc->next;

                this->request_queue_.FreeDesc(index);
                if (!has_next) {
                    break;
                }
                index = next;
            }
            sync_completion_signal(&sync);
        });
        auto status = sync_completion_wait_deadline(&sync, ZX_MSEC(5));
        if (status == ZX_OK) {
            break;
        }
    }

    // If there was either a transport or SCSI level error, return a failure.
    if (response->response || response->status) {
        return ZX_ERR_INTERNAL;
    }

    // Copy data-in region to the caller.
    if (data_in.iov_len) {
        memcpy(data_in.iov_base, data_in_region, data_in.iov_len);
    }

    return ZX_OK;
}

constexpr uint32_t SCSI_SECTOR_SIZE = 512;
constexpr uint32_t SCSI_MAX_XFER_SIZE = 1024; // 512K clamp

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
                                     /*data_in=*/{&vpd_pagelist,
                                     sizeof(vpd_pagelist)});
    if (status != ZX_OK)
        return status;
    uint8_t i;
    for (i = 0 ; i < vpd_pagelist.page_length ; i++) {
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
                                /*data_in=*/{&block_limits,
                                sizeof(block_limits)});
    if (status != ZX_OK)
        return status;
    xfer_size_sectors = block_limits.max_xfer_length_blocks;
    return ZX_OK;
}

zx_status_t ScsiDevice::WorkerThread() {
    uint16_t max_target;
    uint32_t max_lun;
    uint32_t max_sectors; // controller's max sectors.
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
                    max_xfer_size_sectors = fbl::min(max_xfer_size_sectors,
                                                     max_sectors);
                    // and the 512K clamp
                    max_xfer_size_sectors = fbl::min(max_xfer_size_sectors,
                                                     SCSI_MAX_XFER_SIZE);
                } else {
                    max_xfer_size_sectors = fbl::min(max_sectors,
                                                     SCSI_MAX_XFER_SIZE);
                }
                scsi::Disk::Create(this, device_, /*target=*/target, /*lun=*/lun,
                                   max_xfer_size_sectors);
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

    Device::DeviceReset();
    Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, num_queues),
                                       &config_.num_queues);
    Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, seg_max),
                                       &config_.seg_max);
    Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, max_sectors),
                                       &config_.max_sectors);
    Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, cmd_per_lun),
                                       &config_.cmd_per_lun);
    Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, event_info_size),
                                       &config_.event_info_size);
    Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, sense_size),
                                       &config_.sense_size);
    Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, cdb_size),
                                       &config_.cdb_size);
    Device::ReadDeviceConfig<uint16_t>(offsetof(virtio_scsi_config, max_channel),
                                       &config_.max_channel);
    Device::ReadDeviceConfig<uint16_t>(offsetof(virtio_scsi_config, max_target),
                                       &config_.max_target);
    Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, max_lun),
                                       &config_.max_lun);

    // Validate config.
    {
        fbl::AutoLock lock(&lock_);
        if (config_.max_channel > 1) {
            zxlogf(WARN, "config_.max_channel %d not expected.\n", config_.max_channel);
        }
    }

    Device::DriverStatusAck();

    {
        fbl::AutoLock lock(&lock_);
        auto err = control_ring_.Init(/*index=*/Queue::CONTROL);
        if (err) {
            zxlogf(ERROR, "failed to allocate control queue\n");
            return err;
        }

        err = request_queue_.Init(/*index=*/Queue::REQUEST);
        if (err) {
            zxlogf(ERROR, "failed to allocate request queue\n");
            return err;
        }

        // We only queue up 1 command at a time, so we only need space in the io
        // buffer for just 1 scsi req, 1 scsi resp and either data in or out.
        // TODO: The allocation of the IO buffer region for data will go away
        // once we initiate DMA in/out of pages. Then we would need to allocate
        // IO buffer regions for the indirect scatter-gather list of paddrs (we
        // would need as many of those as the # of concurrent IOs).
        const size_t request_buffers_size =
            (SCSI_SECTOR_SIZE *
             fbl::min(config_.max_sectors, SCSI_MAX_XFER_SIZE)) +
            (sizeof(struct virtio_scsi_req_cmd) +
             sizeof(struct virtio_scsi_resp_cmd));
        auto status =
            io_buffer_init(&request_buffers_, bti().get(),
                           /*size=*/request_buffers_size,
                           IO_BUFFER_RW | IO_BUFFER_CONTIG);
        if (status) {
            zxlogf(ERROR, "failed to allocate queue working memory\n");
            return status;
        }
    }

    Device::StartIrqThread();
    Device::DriverStatusOk();

    device_add_args_t args{};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "virtio-scsi";
    args.ops = &device_ops_;
    args.ctx = this;

    // Synchronize against Unbind()/Release() before the worker thread is running.
    fbl::AutoLock lock(&lock_);
    auto status = device_add(Device::bus_device_, &args, &device_);
    if (status != ZX_OK) {
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

void ScsiDevice::Unbind() {
    Device::Unbind();
}

void ScsiDevice::Release() {
    {
        fbl::AutoLock lock(&lock_);
        worker_thread_should_exit_ = true;
    }
    thrd_join(worker_thread_, nullptr);
    Device::Release();
}

} // namespace virtio
